#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace fc {

class FileCollector {
public:
  void CollectFile(uint32_t fileId, size_t fileSize);
  void OnNewChunk(uint32_t fileId, size_t pos, std::vector<uint8_t> &&chunk);
  // pos - позиция в файле

  // Метод получения собранного файла.
  // Требуется предложить и реализовать оптимальный интерфейс.
  // std::vector<uint8_t> GetFile(uint32_t fileId); // Как передавать данные?
private:
  class FileState {
  public:
    std::vector<uint8_t> buffer_;        // size of fileSize
    std::map<size_t, size_t> intervals_; // non‑overlapping merged
    mutable std::mutex mtx_;

    FileState(size_t fileSize);
  };

  std::unordered_map<uint32_t, std::shared_ptr<FileState>> files_;
  mutable std::mutex filesMutex_;
};

} // namespace fc
