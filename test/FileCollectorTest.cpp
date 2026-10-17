#include "../include/FileCollector.hpp"
#include "../include/Exception.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <random>
#include <thread>
#include <utility>
#include <vector>

namespace {

using fc::FileCollector;
using Interval = std::pair<size_t, size_t>; // [first, second)

// ---------- helpers ----------

std::vector<uint8_t> MakeData(size_t size, uint8_t seed = 0) {
  std::vector<uint8_t> d(size);
  for (size_t i = 0; i < size; ++i)
    d[i] = static_cast<uint8_t>((i * 31 + seed * 7 + 1) & 0xFF);
  return d;
}

std::vector<uint8_t> Slice(const std::vector<uint8_t> &d, size_t pos,
                           size_t len) {
  return {d.begin() + pos, d.begin() + pos + len};
}

std::vector<uint8_t> Bytes(std::initializer_list<char> l) {
  return {l.begin(), l.end()};
}

struct Chunk {
  size_t pos;
  std::vector<uint8_t> data;
};

// Chunks of chunkSize every `step` bytes; step < chunkSize -> overlaps.
std::vector<Chunk> MakeChunks(const std::vector<uint8_t> &data,
                              size_t chunkSize, size_t step) {
  std::vector<Chunk> res;
  for (size_t pos = 0; pos < data.size(); pos += step) {
    size_t len = std::min(chunkSize, data.size() - pos);
    res.push_back({pos, Slice(data, pos, len)});
  }
  return res;
}

// Snapshot of filled intervals. Call only when no writers are active:
// GetCollectingFile returns a reference that is read without the file lock.
std::vector<Interval> Intervals(FileCollector &c, uint32_t id) {
  auto cf = c.GetCollectingFile(id);
  const auto &cont = cf.second.get_container();
  return {cont.begin(), cont.end()};
}

bool IsFilled(FileCollector &c, uint32_t id) {
  auto cf = c.GetCollectingFile(id);
  auto iv = Intervals(c, id);
  return iv.size() == 1 && iv[0].first == 0 && iv[0].second == cf.first->size();
}

// ---------- basic scenarios ----------

TEST(FileCollector, ExampleFromTask) {
  FileCollector c;
  c.CollectFile(1, 10);

  c.OnNewChunk(1, 0, Bytes({'A', 'B', 'C', 'D'}));
  c.OnNewChunk(1, 2, Bytes({'C', 'D', 'E', 'F'}));
  EXPECT_EQ(Intervals(c, 1), (std::vector<Interval>{{0, 6}}));

  c.OnNewChunk(1, 6, Bytes({'G', 'H', 'I', 'J'}));
  ASSERT_TRUE(IsFilled(c, 1));

  auto f = c.GetFile(1);
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(*f, Bytes({'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J'}));
}

TEST(FileCollector, NewFileIsZeroedAndHasNoIntervals) {
  FileCollector c;
  c.CollectFile(1, 10);
  auto f = c.GetFile(1);
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(*f, std::vector<uint8_t>(10, 0));
  EXPECT_TRUE(Intervals(c, 1).empty());
  EXPECT_FALSE(IsFilled(c, 1));
}

TEST(FileCollector, SingleChunkCoversWholeFile) {
  FileCollector c;
  auto data = MakeData(100);
  c.CollectFile(1, data.size());
  c.OnNewChunk(1, 0, std::vector<uint8_t>(data));

  ASSERT_TRUE(IsFilled(c, 1));
  EXPECT_EQ(*c.GetFile(1), data);
}

TEST(FileCollector, ReverseOrder) {
  FileCollector c;
  auto data = MakeData(64);
  c.CollectFile(1, data.size());

  auto chunks = MakeChunks(data, 8, 8);
  std::reverse(chunks.begin(), chunks.end());
  for (size_t i = 0; i < chunks.size(); ++i) {
    EXPECT_FALSE(IsFilled(c, 1)) << "filled too early at chunk " << i;
    c.OnNewChunk(1, chunks[i].pos, std::move(chunks[i].data));
  }

  ASSERT_TRUE(IsFilled(c, 1));
  EXPECT_EQ(*c.GetFile(1), data);
}

TEST(FileCollector, AdjacentChunksMerge) {
  FileCollector c;
  auto data = MakeData(10);
  c.CollectFile(1, 10);
  c.OnNewChunk(1, 5, Slice(data, 5, 5));
  c.OnNewChunk(1, 0, Slice(data, 0, 5));

  EXPECT_EQ(Intervals(c, 1), (std::vector<Interval>{{0, 10}}));
  EXPECT_EQ(*c.GetFile(1), data);
}

TEST(FileCollector, GapsAreReportedAsSeparateIntervals) {
  FileCollector c;
  auto data = MakeData(10);
  c.CollectFile(1, 10);
  c.OnNewChunk(1, 7, Slice(data, 7, 3));
  c.OnNewChunk(1, 0, Slice(data, 0, 3));

  EXPECT_EQ(Intervals(c, 1), (std::vector<Interval>{{0, 3}, {7, 10}}));
  auto f = c.GetFile(1);
  EXPECT_EQ(Slice(*f, 0, 3), Slice(data, 0, 3));
  EXPECT_EQ(Slice(*f, 7, 3), Slice(data, 7, 3));
  EXPECT_EQ(Slice(*f, 3, 4), std::vector<uint8_t>(4, 0)); // gap untouched
}

TEST(FileCollector, DuplicatesAreHarmless) {
  FileCollector c;
  auto data = MakeData(20);
  c.CollectFile(1, 20);

  for (int i = 0; i < 5; ++i)
    c.OnNewChunk(1, 0, Slice(data, 0, 10));
  EXPECT_EQ(Intervals(c, 1), (std::vector<Interval>{{0, 10}}));

  c.OnNewChunk(1, 10, Slice(data, 10, 10));
  ASSERT_TRUE(IsFilled(c, 1));
  EXPECT_EQ(*c.GetFile(1), data);
}

TEST(FileCollector, ChunkNestedInsideAnother) {
  FileCollector c;
  auto data = MakeData(10);
  c.CollectFile(1, 10);
  c.OnNewChunk(1, 0, std::vector<uint8_t>(data));
  c.OnNewChunk(1, 3, Slice(data, 3, 2));

  ASSERT_TRUE(IsFilled(c, 1));
  EXPECT_EQ(*c.GetFile(1), data);
}

TEST(FileCollector, ChunkCoveringSeveralIntervals) {
  FileCollector c;
  auto data = MakeData(30);
  c.CollectFile(1, 30);
  c.OnNewChunk(1, 2, Slice(data, 2, 3));   // [2,5)
  c.OnNewChunk(1, 10, Slice(data, 10, 2)); // [10,12)
  c.OnNewChunk(1, 20, Slice(data, 20, 4)); // [20,24)
  EXPECT_EQ(Intervals(c, 1).size(), 3u);

  c.OnNewChunk(1, 0, std::vector<uint8_t>(data)); // swallows all of them
  ASSERT_TRUE(IsFilled(c, 1));
  EXPECT_EQ(*c.GetFile(1), data);
}

TEST(FileCollector, ChunkBridgesGap) {
  FileCollector c;
  auto data = MakeData(10);
  c.CollectFile(1, 10);
  c.OnNewChunk(1, 0, Slice(data, 0, 3)); // [0,3)
  c.OnNewChunk(1, 7, Slice(data, 7, 3)); // [7,10)
  c.OnNewChunk(1, 2, Slice(data, 2, 6)); // [2,8) overlaps both

  ASSERT_TRUE(IsFilled(c, 1));
  EXPECT_EQ(*c.GetFile(1), data);
}

// Already covered bytes must not be rewritten (only uncovered parts copied).
TEST(FileCollector, OverlapWritesOnlyUncoveredBytes) {
  FileCollector c;
  c.CollectFile(1, 6);
  c.OnNewChunk(1, 0, Bytes({'A', 'B', 'C', 'D'}));
  c.OnNewChunk(1, 2, Bytes({'x', 'y', 'E', 'F'}));
  EXPECT_EQ(*c.GetFile(1), Bytes({'A', 'B', 'C', 'D', 'E', 'F'}));
}

TEST(FileCollector, OneByteMissingIsNotFilled) {
  FileCollector c;
  auto data = MakeData(10);
  c.CollectFile(1, 10);
  c.OnNewChunk(1, 0, Slice(data, 0, 9));

  EXPECT_FALSE(IsFilled(c, 1));
  EXPECT_EQ(Intervals(c, 1), (std::vector<Interval>{{0, 9}}));
}

TEST(FileCollector, EmptyChunkDoesNotBreakIntervals) {
  FileCollector c;
  auto data = MakeData(10);
  c.CollectFile(1, 10);
  EXPECT_NO_THROW(c.OnNewChunk(1, 3, {}));
  EXPECT_TRUE(Intervals(c, 1).empty());

  c.OnNewChunk(1, 0, std::vector<uint8_t>(data));
  EXPECT_TRUE(IsFilled(c, 1));
}

TEST(FileCollector, MultipleFilesAreIndependent) {
  FileCollector c;
  auto a = MakeData(50, 1);
  auto b = MakeData(70, 2);
  c.CollectFile(1, a.size());
  c.CollectFile(2, b.size());

  c.OnNewChunk(1, 0, std::vector<uint8_t>(a));
  EXPECT_TRUE(IsFilled(c, 1));
  EXPECT_FALSE(IsFilled(c, 2));

  c.OnNewChunk(2, 0, std::vector<uint8_t>(b));
  EXPECT_EQ(*c.GetFile(1), a);
  EXPECT_EQ(*c.GetFile(2), b);
}

TEST(FileCollector, ReturnedFileOutlivesFurtherOperations) {
  FileCollector c;
  auto data = MakeData(32);
  c.CollectFile(1, data.size());
  c.OnNewChunk(1, 0, std::vector<uint8_t>(data));
  auto f = c.GetFile(1);

  for (uint32_t id = 100; id < 1100; ++id) // force map rehash
    c.CollectFile(id, 16);

  ASSERT_NE(f, nullptr);
  EXPECT_EQ(*f, data);
}

TEST(FileCollector, LargeFileShuffledOverlappingChunks) {
  FileCollector c;
  auto data = MakeData(1 << 20);
  c.CollectFile(1, data.size());

  auto chunks = MakeChunks(data, 4096, 3000);
  std::mt19937 rng(123);
  std::shuffle(chunks.begin(), chunks.end(), rng);
  for (auto &ch : chunks)
    c.OnNewChunk(1, ch.pos, std::move(ch.data));

  ASSERT_TRUE(IsFilled(c, 1));
  EXPECT_EQ(*c.GetFile(1), data);
}

// ---------- CollectFile with an existing id ----------

TEST(FileCollector, CollectFileSameIdDifferentSizeThrows) {
  FileCollector c;
  c.CollectFile(1, 10);
  EXPECT_THROW(c.CollectFile(1, 20), fc::Exception);
}

TEST(FileCollector, CollectFileSameIdSameSizeKeepsData) {
  FileCollector c;
  auto data = MakeData(10);
  c.CollectFile(1, 10);
  c.OnNewChunk(1, 0, Slice(data, 0, 5));

  EXPECT_NO_THROW(c.CollectFile(1, 10));
  EXPECT_EQ(Intervals(c, 1), (std::vector<Interval>{{0, 5}}));
  EXPECT_EQ(Slice(*c.GetFile(1), 0, 5), Slice(data, 0, 5));
}

// ---------- errors ----------

TEST(FileCollector, OnNewChunkUnknownIdThrows) {
  FileCollector c;
  EXPECT_THROW(c.OnNewChunk(42, 0, MakeData(4)), fc::Exception);
}

TEST(FileCollector, GetFileUnknownIdThrows) {
  FileCollector c;
  EXPECT_THROW(c.GetFile(42), fc::Exception);
}

TEST(FileCollector, GetCollectingFileUnknownIdThrows) {
  FileCollector c;
  EXPECT_THROW(c.GetCollectingFile(42), fc::Exception);
}

TEST(FileCollector, ChunkStartingPastEndThrows) {
  FileCollector c;
  c.CollectFile(1, 10);
  EXPECT_THROW(c.OnNewChunk(1, 20, MakeData(5)), fc::Exception);
  EXPECT_TRUE(Intervals(c, 1).empty());
}

TEST(FileCollector, ChunkCrossingEndThrows) {
  FileCollector c;
  c.CollectFile(1, 10);
  EXPECT_THROW(c.OnNewChunk(1, 8, MakeData(5)), fc::Exception);
  EXPECT_TRUE(Intervals(c, 1).empty());
}

// ---------- multithreaded ----------
// Intervals / file contents are inspected only after all writers joined.

TEST(FileCollectorMT, ConcurrentChunksSameFile) {
  constexpr int kThreads = 8;
  FileCollector c;
  auto data = MakeData(256 * 1024);
  c.CollectFile(1, data.size());

  auto chunks = MakeChunks(data, 1000, 700);
  auto copy = chunks; // every chunk arrives twice
  chunks.insert(chunks.end(), copy.begin(), copy.end());
  std::mt19937 rng(42);
  std::shuffle(chunks.begin(), chunks.end(), rng);

  std::atomic<size_t> next{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t)
    threads.emplace_back([&] {
      for (size_t i; (i = next.fetch_add(1)) < chunks.size();)
        c.OnNewChunk(1, chunks[i].pos, std::move(chunks[i].data));
    });
  for (auto &th : threads)
    th.join();

  ASSERT_TRUE(IsFilled(c, 1));
  EXPECT_EQ(*c.GetFile(1), data);
}

TEST(FileCollectorMT, ConcurrentDifferentFiles) {
  constexpr uint32_t kFiles = 16;
  FileCollector c;
  std::vector<std::vector<uint8_t>> files;
  for (uint32_t i = 0; i < kFiles; ++i)
    files.push_back(MakeData(10000 + i * 100, static_cast<uint8_t>(i)));

  std::vector<std::thread> threads;
  for (uint32_t id = 0; id < kFiles; ++id)
    threads.emplace_back([&, id] {
      c.CollectFile(id, files[id].size()); // map insertions race too
      auto chunks = MakeChunks(files[id], 512, 400);
      std::mt19937 rng(id);
      std::shuffle(chunks.begin(), chunks.end(), rng);
      for (auto &ch : chunks)
        c.OnNewChunk(id, ch.pos, std::move(ch.data));
    });
  for (auto &th : threads)
    th.join();

  for (uint32_t id = 0; id < kFiles; ++id) {
    ASSERT_TRUE(IsFilled(c, id)) << "file " << id;
    EXPECT_EQ(*c.GetFile(id), files[id]) << "file " << id;
  }
}

TEST(FileCollectorMT, ManyThreadsManyFilesInterleaved) {
  constexpr uint32_t kFiles = 8;
  constexpr int kThreads = 8;
  FileCollector c;
  std::vector<std::vector<uint8_t>> files;
  std::vector<std::pair<uint32_t, Chunk>> work;
  for (uint32_t id = 0; id < kFiles; ++id) {
    files.push_back(MakeData(20000, static_cast<uint8_t>(id)));
    c.CollectFile(id, files[id].size());
    for (auto &ch : MakeChunks(files[id], 300, 200))
      work.emplace_back(id, std::move(ch));
  }
  std::mt19937 rng(7);
  std::shuffle(work.begin(), work.end(), rng);

  std::atomic<size_t> next{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t)
    threads.emplace_back([&] {
      for (size_t i; (i = next.fetch_add(1)) < work.size();)
        c.OnNewChunk(work[i].first, work[i].second.pos,
                     std::move(work[i].second.data));
    });
  for (auto &th : threads)
    th.join();

  for (uint32_t id = 0; id < kFiles; ++id) {
    ASSERT_TRUE(IsFilled(c, id));
    EXPECT_EQ(*c.GetFile(id), files[id]);
  }
}

// Writers on file 1 while another thread keeps registering new files
// (map rehashes under OnNewChunk) and re-registering file 1 with same size.
TEST(FileCollectorMT, WritersWhileOtherFilesRegistered) {
  FileCollector c;
  auto data = MakeData(64 * 1024);
  c.CollectFile(1, data.size());
  auto chunks = MakeChunks(data, 512, 384);

  std::atomic<size_t> next{0};
  std::atomic<bool> done{false};
  std::thread registrar([&] {
    for (uint32_t id = 1000; !done.load(); ++id) {
      c.CollectFile(id, 8);
      c.CollectFile(1, data.size()); // same size: must be a no-op
    }
  });

  std::vector<std::thread> writers;
  for (int t = 0; t < 4; ++t)
    writers.emplace_back([&] {
      for (size_t i; (i = next.fetch_add(1)) < chunks.size();)
        c.OnNewChunk(1, chunks[i].pos, std::move(chunks[i].data));
    });
  for (auto &th : writers)
    th.join();
  done = true;
  registrar.join();

  ASSERT_TRUE(IsFilled(c, 1));
  EXPECT_EQ(*c.GetFile(1), data);
}

} // namespace
