#include "FileCollector.hpp"
#include "Exception.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <latch>
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

// ---------- more multithreaded ----------

// Starts n threads that all begin fn(i) at the same moment.
void RunTogether(int n, const std::function<void(int)> &fn) {
  std::latch start(n);
  std::vector<std::thread> threads;
  threads.reserve(n);
  for (int i = 0; i < n; ++i)
    threads.emplace_back([&, i] {
      start.arrive_and_wait();
      fn(i);
    });
  for (auto &th : threads)
    th.join();
}

constexpr int kRepeats = 50; // races are rare, repeat small scenarios

// All threads register the same file with the same size, then write
// their own part. No one may throw, no one may wipe other's data.
TEST(FileCollectorMT, CollectFileSameIdSameSizeRace) {
  constexpr int kThreads = 8;
  constexpr size_t kPart = 128;
  auto data = MakeData(kThreads * kPart);

  for (int rep = 0; rep < kRepeats; ++rep) {
    FileCollector c;
    std::atomic<int> errors{0};
    RunTogether(kThreads, [&](int i) {
      try {
        c.CollectFile(1, data.size());
        c.OnNewChunk(1, i * kPart, Slice(data, i * kPart, kPart));
      } catch (...) {
        ++errors;
      }
    });
    ASSERT_EQ(errors.load(), 0) << "rep " << rep;
    ASSERT_TRUE(IsFilled(c, 1)) << "rep " << rep;
    ASSERT_EQ(*c.GetFile(1), data) << "rep " << rep;
  }
}

// Threads register the same id with different sizes:
// exactly one wins, the rest get fc::Exception.
TEST(FileCollectorMT, CollectFileSameIdDifferentSizeRace) {
  constexpr int kThreads = 8;
  for (int rep = 0; rep < kRepeats; ++rep) {
    FileCollector c;
    std::atomic<int> ok{0}, failed{0};
    std::atomic<size_t> winnerSize{0};
    RunTogether(kThreads, [&](int i) {
      size_t size = 100 + i;
      try {
        c.CollectFile(1, size);
        ++ok;
        winnerSize = size;
      } catch (const fc::Exception &) {
        ++failed;
      }
    });
    ASSERT_EQ(ok.load(), 1) << "rep " << rep;
    ASSERT_EQ(failed.load(), kThreads - 1) << "rep " << rep;
    ASSERT_EQ(c.GetFile(1)->size(), winnerSize.load()) << "rep " << rep;
  }
}

// Every byte written by its own thread: maximum contention on one mutex
// and on interval merging.
TEST(FileCollectorMT, SingleByteChunksContention) {
  constexpr int kThreads = 8;
  constexpr size_t kSize = 4096;
  FileCollector c;
  auto data = MakeData(kSize);
  c.CollectFile(1, kSize);

  RunTogether(kThreads, [&](int t) {
    for (size_t pos = t; pos < kSize; pos += kThreads)
      c.OnNewChunk(1, pos, {data[pos]});
  });

  EXPECT_EQ(Intervals(c, 1), (std::vector<Interval>{{0, kSize}}));
  EXPECT_EQ(*c.GetFile(1), data);
}

// Each thread independently sends random overlapping chunks that together
// cover the whole file; all threads overlap with each other everywhere.
TEST(FileCollectorMT, EveryThreadSendsWholeFileRandomly) {
  constexpr int kThreads = 8;
  FileCollector c;
  auto data = MakeData(100000);
  c.CollectFile(1, data.size());

  RunTogether(kThreads, [&](int t) {
    std::mt19937 rng(1000 + t);
    std::uniform_int_distribution<size_t> lenDist(1, 2000);
    std::vector<Chunk> chunks;
    for (size_t pos = 0; pos < data.size();) {
      size_t len = std::min(lenDist(rng), data.size() - pos);
      // start a bit earlier to create overlaps inside one thread too
      size_t start = pos >= len / 2 ? pos - len / 2 : 0;
      size_t end = pos + len;
      chunks.push_back({start, Slice(data, start, end - start)});
      pos = end;
    }
    std::shuffle(chunks.begin(), chunks.end(), rng);
    for (auto &ch : chunks)
      c.OnNewChunk(1, ch.pos, std::move(ch.data));
  });

  ASSERT_TRUE(IsFilled(c, 1));
  EXPECT_EQ(*c.GetFile(1), data);
}

// Invalid calls (unknown id, out of range) from some threads must throw
// and must not corrupt the file being written by other threads.
TEST(FileCollectorMT, ErrorsDoNotCorruptConcurrentWrites) {
  constexpr int kWriters = 4;
  constexpr int kBadThreads = 4;
  constexpr int kBadCallsPerThread = 1000;
  FileCollector c;
  auto data = MakeData(64 * 1024);
  c.CollectFile(1, data.size());
  auto chunks = MakeChunks(data, 700, 500);

  std::atomic<size_t> next{0};
  std::atomic<int> thrown{0};
  RunTogether(kWriters + kBadThreads, [&](int t) {
    if (t < kWriters) {
      for (size_t i; (i = next.fetch_add(1)) < chunks.size();)
        c.OnNewChunk(1, chunks[i].pos, std::move(chunks[i].data));
      return;
    }
    for (int k = 0; k < kBadCallsPerThread; ++k) {
      try {
        if (k % 3 == 0)
          c.OnNewChunk(777, 0, MakeData(4)); // unknown id
        else if (k % 3 == 1)
          c.OnNewChunk(1, data.size() - 2, MakeData(10)); // crosses end
        else
          c.OnNewChunk(1, data.size() + 5, MakeData(1)); // past end
      } catch (const fc::Exception &) {
        ++thrown;
      }
    }
  });

  EXPECT_EQ(thrown.load(), kBadThreads * kBadCallsPerThread);
  ASSERT_TRUE(IsFilled(c, 1));
  EXPECT_EQ(*c.GetFile(1), data);
}

// Readers take the shared_ptr while writers work. Only the pointer and the
// (immutable) size are touched, contents are compared after join.
TEST(FileCollectorMT, GetFilePointerWhileWriting) {
  constexpr int kWriters = 4;
  constexpr int kReaders = 4;
  FileCollector c;
  auto data = MakeData(64 * 1024);
  c.CollectFile(1, data.size());
  auto chunks = MakeChunks(data, 1024, 1024);
  std::mt19937 rng(5);
  std::shuffle(chunks.begin(), chunks.end(), rng);

  std::atomic<size_t> next{0};
  std::atomic<bool> writersDone{false};
  std::atomic<int> writersLeft{kWriters};
  std::atomic<int> badPtr{0};
  RunTogether(kWriters + kReaders, [&](int t) {
    if (t < kWriters) {
      for (size_t i; (i = next.fetch_add(1)) < chunks.size();)
        c.OnNewChunk(1, chunks[i].pos, std::move(chunks[i].data));
      if (--writersLeft == 0)
        writersDone = true;
      return;
    }
    while (!writersDone) {
      auto f = c.GetFile(1);
      if (!f || f->size() != data.size())
        ++badPtr;
    }
  });

  EXPECT_EQ(badPtr.load(), 0);
  ASSERT_TRUE(IsFilled(c, 1));
  EXPECT_EQ(*c.GetFile(1), data);
}

// Many files registered and written concurrently, each by several threads.
TEST(FileCollectorMT, RegisterAndWriteManyFilesTogether) {
  constexpr uint32_t kFiles = 64;
  constexpr int kThreadsPerFile = 3;
  FileCollector c;
  std::vector<std::vector<uint8_t>> files;
  for (uint32_t id = 0; id < kFiles; ++id)
    files.push_back(MakeData(3000 + id * 17, static_cast<uint8_t>(id)));

  RunTogether(kFiles * kThreadsPerFile, [&](int t) {
    uint32_t id = static_cast<uint32_t>(t) / kThreadsPerFile;
    int part = t % kThreadsPerFile;
    c.CollectFile(id, files[id].size()); // same id from several threads
    auto chunks = MakeChunks(files[id], 256, 200);
    for (size_t i = part; i < chunks.size(); i += kThreadsPerFile)
      c.OnNewChunk(id, chunks[i].pos, std::move(chunks[i].data));
  });

  for (uint32_t id = 0; id < kFiles; ++id) {
    ASSERT_TRUE(IsFilled(c, id)) << "file " << id;
    EXPECT_EQ(*c.GetFile(id), files[id]) << "file " << id;
  }
}

} // namespace
