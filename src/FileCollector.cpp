#include "FileCollector.hpp"
#include "Exception.hpp"

#include <algorithm>
#include <format>
#include <iterator>
#include <mutex>
#include <shared_mutex>

namespace fc {

FileCollector::FileState::FileState(size_t fileSize)
    : data(std::make_shared<std::vector<uint8_t>>(fileSize)), size(fileSize) {
  // An empty file is complete from the start.
  complete.store(fileSize == 0, std::memory_order_relaxed);
}

void FileCollector::CollectFile(uint32_t fileId, size_t fileSize) {
  std::unique_lock lock(filesMutex_);
  auto it = files_.find(fileId);
  if (it != files_.end()) {
    const size_t realSize = it->second->size;
    if (realSize != fileSize) {
      throw Exception(
          ErrorCode::FileIdAlreadyUse,
          std::format("File {} with size {} already exists", fileId, realSize));
    }
    return; // same file - nothing to do
  }
  files_.emplace(fileId, std::make_shared<FileState>(fileSize));
}

std::shared_ptr<FileCollector::FileState>
FileCollector::FindState(uint32_t fileId) const {
  std::shared_lock lock(filesMutex_);
  auto it = files_.find(fileId);
  if (it == files_.end()) {
    throw Exception(ErrorCode::FileNotExist,
                    std::format("File {} not exist", fileId));
  }
  return it->second; // keeps FileState alive outside the lock
}

void FileCollector::OnNewChunk(uint32_t fileId, size_t pos,
                               std::vector<uint8_t> &&chunk) {
  auto state = FindState(fileId);

  // Overflow-safe check: pos + chunk.size() <= size
  if (pos > state->size || chunk.size() > state->size - pos) {
    throw Exception(ErrorCode::ChunkOutOfRange,
                    std::format("Chunk [{}, {}) is out of file {} size {}", pos,
                                pos + chunk.size(), fileId, state->size));
  }
  // Empty chunk or already complete file: nothing to write.
  if (chunk.empty() || state->complete.load(std::memory_order_acquire)) {
    return;
  }

  const size_t end = pos + chunk.size();
  std::lock_guard lock(state->mtx);
  auto uncovered = state->intervals.uncovered_intervals({pos, end});
  if (uncovered.empty()) {
    return; // duplicate
  }
  for (const auto &[from, to] : uncovered) {
    std::copy(chunk.begin() + static_cast<std::ptrdiff_t>(from - pos),
              chunk.begin() + static_cast<std::ptrdiff_t>(to - pos),
              state->data->begin() + static_cast<std::ptrdiff_t>(from));
  }
  // All uncovered ranges are written, so the whole [pos, end) is covered:
  // one insertion is enough instead of one per piece.
  state->intervals.add_interval({pos, end});

  const auto &c = state->intervals.get_container();
  if (c.size() == 1 && c.begin()->first == 0 &&
      c.begin()->second == state->size) {
    state->complete.store(true, std::memory_order_release);
  }
}

bool FileCollector::IsComplete(uint32_t fileId) const {
  return FindState(fileId)->complete.load(std::memory_order_acquire);
}

size_t FileCollector::GetFileSize(uint32_t fileId) const {
  return FindState(fileId)->size;
}

FileData FileCollector::GetFile(uint32_t fileId) const {
  auto state = FindState(fileId);
  // acquire pairs with the release in OnNewChunk: all writes to the buffer
  // are visible and no more writes will happen, so no file lock is needed.
  if (!state->complete.load(std::memory_order_acquire)) {
    throw Exception(ErrorCode::FileNotFilled,
                    std::format("File {} is not filled yet", fileId));
  }
  return state->data; // implicitly converts to shared_ptr<const vector>
}

size_t FileCollector::Read(uint32_t fileId, size_t pos,
                           std::span<uint8_t> out) const {
  auto state = FindState(fileId);
  if (pos > state->size) {
    throw Exception(ErrorCode::ChunkOutOfRange,
                    std::format("Read position {} is out of file {} size {}",
                                pos, fileId, state->size));
  }
  if (out.empty() || pos == state->size) {
    return 0;
  }

  const auto &buf = *state->data;
  auto copy = [&](size_t availableEnd) {
    const size_t n = std::min(out.size(), availableEnd - pos);
    std::copy_n(buf.begin() + static_cast<std::ptrdiff_t>(pos), n, out.begin());
    return n;
  };

  // Fast path: a complete file never changes, no lock needed.
  if (state->complete.load(std::memory_order_acquire)) {
    return copy(state->size);
  }

  std::lock_guard lock(state->mtx);
  const auto &c = state->intervals.get_container();
  // The interval containing pos: the last one starting at <= pos.
  auto it = c.upper_bound(pos);
  if (it == c.begin()) {
    return 0;
  }
  --it;
  if (it->second <= pos) {
    return 0; // pos is in a gap
  }
  return copy(it->second);
}

std::vector<Interval<size_t>>
FileCollector::GetFilledIntervals(uint32_t fileId) const {
  auto state = FindState(fileId);
  std::lock_guard lock(state->mtx);
  const auto &c = state->intervals.get_container();
  return {c.begin(), c.end()};
}

} // namespace fc
