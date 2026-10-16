#include "../include/FileCollector.hpp"
#include "../include/Exception.hpp"
#include "../include/NonOverlappingIntervals.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <utility>

using namespace fc;

FileCollector::FileState::FileState(size_t fileSize)
    : file_(std::make_shared<std::vector<uint8_t>>(fileSize)) {}

void FileCollector::CollectFile(uint32_t fileId, size_t fileSize) {
  std::lock_guard<std::mutex> lock(filesMutex_);
  auto it = files_.find(fileId);
  if (it != files_.end()) { // if the file already exists
    auto real_size = it->second->file_->size();
    if (real_size != fileSize) { // check fileSize
      // it isn't the same file
      throw Exception(ErrorCode::FileIdAlreadyUse,
                      std::format("File {} with size {} already exists", fileId,
                                  real_size));
    }
  }
  files_[fileId] = std::make_shared<FileState>(fileSize);
}
void FileCollector::OnNewChunk(uint32_t fileId, size_t pos,
                               std::vector<uint8_t> &&chunk) {
  // check start_chunk, end_chunk in intervals
  // записать, где нет пересечений
  std::unique_lock<std::mutex> lock_collector(filesMutex_);
  auto it = files_.find(fileId);
  if (it == files_.end()) {
    throw Exception(ErrorCode::FileNotExist,
                    std::format("File {} not exist", fileId));
  }

  auto &mutex = it->second->mtx_;
  auto &intervals = it->second->intervals_;
  auto &file = it->second->file_;

  std::lock_guard<std::mutex> lock_file(mutex);
  lock_collector.unlock();

  auto uncovered_intervals =
      intervals.uncovered_intervals(std::make_pair(pos, pos + chunk.size()));

  // file write + add each interval
  for (const auto &interval : uncovered_intervals) {
    auto input_begin =
        chunk.begin() + static_cast<std::ptrdiff_t>(interval.first - pos);
    auto input_end =
        file->begin() + static_cast<std::ptrdiff_t>(interval.second - pos);
    auto output_begin =
        file->begin() + static_cast<std::ptrdiff_t>(interval.first);

    std::copy(input_begin, input_end, output_begin);

    intervals.add_interval(interval);
  }
}

bool FileCollector::is_filled(uint32_t fileId) {
  std::unique_lock<std::mutex> lock_collector(filesMutex_);
  auto it = files_.find(fileId);
  if (it == files_.end()) { // if file not exists
    throw Exception(ErrorCode::FileNotExist,
                    std::format("File {} not exists", fileId));
  }

  auto &mutex = it->second->mtx_;
  auto &intervals = it->second->intervals_;
  auto &file = it->second->file_;
  std::lock_guard<std::mutex> lock_file(mutex);
  lock_collector.unlock();

  if (intervals.size() == 1) {
    // if exists only one interval, diaposone of fileSize
    auto start = intervals.get_container().begin()->first;
    auto end = intervals.get_container().begin()->second;
    return (end - start) == file->size();
  }
  return false;
}

File FileCollector::GetFile(uint32_t fileId) {
  // если файл уже заполненный то возвращаем Файл
  // если нет - ошибка
  if (is_filled(fileId)) {
    std::lock_guard<std::mutex> lock_collector(filesMutex_);
    auto it = files_.find(fileId);
    return it->second->file_;
  }
  throw Exception(
      ErrorCode::FileNotFilled,
      std::format("File {} not filled \n Try GetCollectingFile(size_t fileId) "
                  "to get view on unfilled file",
                  fileId));
}

CollectingFile FileCollector::GetCollectingFile(uint32_t fileId) {
  std::lock_guard<std::mutex> lock_collector(filesMutex_);
  auto it = files_.find(fileId);
  const auto &file = it->second->file_;
  const auto &intervals = it->second->intervals_;
  return std::make_pair(file, intervals);
}

// Метод получения собранного файла.
// Требуется предложить и реализовать оптимальный интерфейс.
// std::vector<uint8_t> GetFile(uint32_t fileId); // Как передавать данные?
