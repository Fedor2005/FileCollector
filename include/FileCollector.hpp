#pragma once

#include "NonOverlappingIntervals.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <unordered_map>
#include <vector>

namespace fc {

// A fully collected file. Once collection is complete the buffer never
// changes, so it can be shared with any number of readers without copying.
using FileData = std::shared_ptr<const std::vector<uint8_t>>;

class FileCollector {
public:
  // Registers a file. Calling again with the same size is a no-op;
  // a different size throws ErrorCode::FileIdAlreadyUse.
  void CollectFile(uint32_t fileId, size_t fileSize);

  // Adds the chunk [pos, pos + chunk.size()). Bytes that are already written
  // are not overwritten; only uncovered ranges are copied.
  void OnNewChunk(uint32_t fileId, size_t pos, std::vector<uint8_t> &&chunk);

  // ---------- reading (all methods are thread-safe) ----------

  // true if every byte of the file has been received.
  bool IsComplete(uint32_t fileId) const;

  // File size passed to CollectFile.
  size_t GetFileSize(uint32_t fileId) const;

  // The whole file, without copying. Throws ErrorCode::FileNotFilled if the
  // file is not complete yet: a partially collected buffer is never exposed,
  // because other threads are still writing into it.
  FileData GetFile(uint32_t fileId) const;

  // Partial read: copies into out the contiguous already-received range
  // starting at pos (at most out.size() bytes). Returns the number of bytes
  // copied; 0 if the byte at pos has not been received yet.
  // pos > file size -> ErrorCode::ChunkOutOfRange.
  size_t Read(uint32_t fileId, size_t pos, std::span<uint8_t> out) const;

  // Snapshot of filled intervals [begin, end), sorted by begin.
  std::vector<Interval<size_t>> GetFilledIntervals(uint32_t fileId) const;

private:
  struct FileState {
    explicit FileState(size_t fileSize);

    // The pointer never changes after construction; the contents are written
    // under mtx and never change once complete == true.
    const std::shared_ptr<std::vector<uint8_t>> data;
    const size_t size;

    NonOverlappingIntervals<size_t> intervals; // guarded by mtx
    mutable std::mutex mtx;

    // Set under mtx (release) after the last byte is written.
    // A reader that observes true (acquire) sees the whole file.
    std::atomic<bool> complete{false};
  };

  std::shared_ptr<FileState> FindState(uint32_t fileId) const;

  std::unordered_map<uint32_t, std::shared_ptr<FileState>> files_;
  // File lookup (the frequent operation) takes a shared lock,
  // registering a new file takes an exclusive one.
  mutable std::shared_mutex filesMutex_;
};

} // namespace fc
