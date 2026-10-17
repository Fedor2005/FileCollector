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
  if (it != files_.end()) {
    auto real_size = it->second->file_->size();
    if (real_size != fileSize) {
      throw Exception(ErrorCode::FileIdAlreadyUse,
                      std::format("File {} with size {} already exists", fileId,
                                  real_size));
    }
    return; // тот же файл — ничего не делаем
  }
  files_.emplace(fileId, std::make_shared<FileState>(fileSize));
}

void FileCollector::OnNewChunk(uint32_t fileId, size_t pos,
                               std::vector<uint8_t> &&chunk) {
  std::shared_ptr<FileState> state;
  {
    std::lock_guard<std::mutex> lock_collector(filesMutex_);
    auto it = files_.find(fileId);
    if (it == files_.end()) {
      throw Exception(ErrorCode::FileNotExist,
                      std::format("File {} not exist", fileId));
    }
    state = it->second; // держим FileState живым
  }

  const size_t fileSize = state->file_->size(); // размер не меняется
  // запись без переполнения: pos + chunk.size() <= fileSize
  if (pos > fileSize || chunk.size() > fileSize - pos) {
    throw Exception(ErrorCode::ChunkOutOfRange,
                    std::format("Chunk [{}, {}) is out of file {} size {}", pos,
                                pos + chunk.size(), fileId, fileSize));
  }
  if (chunk.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock_file(state->mtx_);
  auto uncovered =
      state->intervals_.uncovered_intervals({pos, pos + chunk.size()});
  for (const auto &interval : uncovered) {
    auto in_begin =
        chunk.begin() + static_cast<std::ptrdiff_t>(interval.first - pos);
    auto in_end = in_begin +
                  static_cast<std::ptrdiff_t>(interval.second - interval.first);
    std::copy(in_begin, in_end,
              state->file_->begin() +
                  static_cast<std::ptrdiff_t>(interval.first));
    state->intervals_.add_interval(interval);
  }
}

File FileCollector::GetFile(uint32_t fileId) {
  // если файл уже заполненный то возвращаем Файл
  // если нет - ошибка
  std::lock_guard<std::mutex> lock_collector(filesMutex_);
  auto it = files_.find(fileId);
  if (it == files_.end()) {
    throw Exception(ErrorCode::FileNotExist,
                    std::format("File {} not exist", fileId));
  }
  return it->second->file_;
}

CollectingFile FileCollector::GetCollectingFile(uint32_t fileId) {
  std::lock_guard<std::mutex> lock_collector(filesMutex_);
  auto it = files_.find(fileId);
  if (it == files_.end()) {
    throw Exception(ErrorCode::FileNotExist,
                    std::format("File {} not exist", fileId));
  }
  return CollectingFile(it->second->file_, it->second->intervals_);
}

// Метод получения собранного файла.
// Требуется предложить и реализовать оптимальный интерфейс.
// std::vector<uint8_t> GetFile(uint32_t fileId); // Как передавать данные?
