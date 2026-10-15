#include "../include/FileCollector.hpp"
#include "../include/Exception.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>

using namespace fc;

FileCollector::FileState::FileState(size_t fileSize)
    : buffer_(std::vector<uint8_t>(fileSize)) {}

void FileCollector::CollectFile(uint32_t fileId, size_t fileSize) {
  std::lock_guard<std::mutex> lock(filesMutex_);
  auto it = files_.find(fileId);
  if (it != files_.end()) { // if the file already exists
    auto real_size = it->second->buffer_.size();
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
  //
}

// Метод получения собранного файла.
// Требуется предложить и реализовать оптимальный интерфейс.
// std::vector<uint8_t> GetFile(uint32_t fileId); // Как передавать данные?
