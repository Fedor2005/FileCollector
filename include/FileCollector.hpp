#pragma once

#include "NonOverlappingIntervals.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace fc {
using File = std::shared_ptr<std::vector<uint8_t>>;
using CollectingFile = std::pair<std::shared_ptr<const std::vector<uint8_t>>,
                                 const NonOverlappingIntervals<size_t> &>;

class FileCollector {
public:
  void CollectFile(uint32_t fileId, size_t fileSize);
  void OnNewChunk(uint32_t fileId, size_t pos, std::vector<uint8_t> &&chunk);
  // pos - позиция в файле

  // Возвращает файл. Если файл не заполенен, ошибка.
  File GetFile(uint32_t FileId);

  // немедленный возврат ссылки на констатные файл + интервалы, на которых он
  // заполнен
  CollectingFile GetCollectingFile(uint32_t fileId);

  //

private:
  class FileState {
  public:
    File file_; // File
    // struct what can store vector and Mapped File
    NonOverlappingIntervals<size_t> intervals_; // non‑overlapping merged
    mutable std::mutex mtx_;

    FileState(size_t fileSize);
  };

  std::unordered_map<uint32_t, std::shared_ptr<FileState>> files_;
  mutable std::mutex filesMutex_;
};

} // namespace fc
