// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "kudu/fs/file_block_manager.h"
#include "kudu/fs/log_block_manager.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/strings/split.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/atomic.h"
#include "kudu/util/metrics.h"
#include "kudu/util/random.h"
#include "kudu/util/test_util.h"
#include "kudu/util/thread.h"

DEFINE_int32(test_duration_secs, 2, "Number of seconds to run the test");
DEFINE_int32(num_writer_threads, 4, "Number of writer threads to run");
DEFINE_int32(num_reader_threads, 8, "Number of reader threads to run");
DEFINE_int32(num_deleter_threads, 1, "Number of deleter threads to run");
DEFINE_int32(block_group_size, 8, "Number of blocks to write per block "
             "group. Must be power of 2");
DEFINE_int32(block_group_bytes, 64 * 1024,
             "Total amount of data (in bytes) to write per block group");
DEFINE_int32(num_bytes_per_write, 64,
             "Number of bytes to write at a time");
DEFINE_string(block_manager_paths, "", "Comma-separated list of paths to "
              "use for block storage. If empty, will use the default unit "
              "test path");

using std::shared_ptr;
using std::string;
using std::vector;
using strings::Substitute;

namespace kudu {
namespace fs {

// This test attempts to simulate how a TS might use the block manager:
//
// writing threads (default 2) that do the following in a tight loop:
// - create a new group of blocks (default 10)
// - write a PRNG seed into each block
// - write a big chunk of data (default 64m) into the block group:
//   - pick the next block to write a piece to at random
//   - write one piece at a time (default 64k) of data generated using
//     that block's PRNG seed
// - close the blocks
// - add the blocks to the block_id vector (write locked)
// reading threads (default 8) that do the following in a tight loop:
// - read one block id at random from block_id vector (read locked)
// - read the block fully into memory, parsing its seed
// - verify that the contents of the block match the PRNG output
// deleting threads (default 1) that do the following every second:
// - drain the block_id vector(write locked)
// - delete all the blocks drained from the vector
//
// TODO: Don't delete all blocks ala "permgen".
template <typename T>
class BlockManagerStressTest : public KuduTest {
 public:
  BlockManagerStressTest() :
    rand_seed_(SeedRandom()),
    stop_latch_(1),
    bm_(CreateBlockManager()),
    total_blocks_written_(0),
    total_bytes_written_(0),
    total_blocks_read_(0),
    total_bytes_read_(0),
    total_blocks_deleted_(0) {
  }

  virtual void SetUp() OVERRIDE {
    CHECK_OK(bm_->Create());
    CHECK_OK(bm_->Open());
  }

  virtual void TearDown() OVERRIDE {
    // If non-standard paths were provided we need to delete them in
    // between test runs.
    if (!FLAGS_block_manager_paths.empty()) {
      vector<string> paths = strings::Split(FLAGS_block_manager_paths, ",",
                                            strings::SkipEmpty());
      for (const string& path : paths) {
        WARN_NOT_OK(env_->DeleteRecursively(path),
                    Substitute("Couldn't recursively delete $0", path));
      }
    }
  }

  BlockManager* CreateBlockManager() {
    BlockManagerOptions opts;
    if (FLAGS_block_manager_paths.empty()) {
      opts.root_paths.push_back(GetTestDataDirectory());
    } else {
      opts.root_paths = strings::Split(FLAGS_block_manager_paths, ",",
                                       strings::SkipEmpty());
    }
    return new T(env_.get(), opts);
  }

  void RunTest(int secs) {
    LOG(INFO) << "Starting all threads";
    this->StartThreads();
    SleepFor(MonoDelta::FromSeconds(secs));
    LOG(INFO) << "Stopping all threads";
    this->StopThreads();
    this->JoinThreads();
    this->stop_latch_.Reset(1);
  }

  void StartThreads() {
    scoped_refptr<Thread> new_thread;
    for (int i = 0; i < FLAGS_num_writer_threads; i++) {
      CHECK_OK(Thread::Create("BlockManagerStressTest", Substitute("writer-$0", i),
                              &BlockManagerStressTest::WriterThread, this, &new_thread));
      threads_.push_back(new_thread);
    }
    for (int i = 0; i < FLAGS_num_reader_threads; i++) {
      CHECK_OK(Thread::Create("BlockManagerStressTest", Substitute("reader-$0", i),
                              &BlockManagerStressTest::ReaderThread, this, &new_thread));
      threads_.push_back(new_thread);
    }
    for (int i = 0; i < FLAGS_num_deleter_threads; i++) {
      CHECK_OK(Thread::Create("BlockManagerStressTest", Substitute("deleter-$0", i),
                              &BlockManagerStressTest::DeleterThread, this, &new_thread));
      threads_.push_back(new_thread);
    }
  }

  void StopThreads() {
    stop_latch_.CountDown();
  }

  bool ShouldStop(const MonoDelta& wait_time) {
    return stop_latch_.WaitFor(wait_time);
  }

  void JoinThreads() {
    for (const scoped_refptr<kudu::Thread>& thr : threads_) {
     CHECK_OK(ThreadJoiner(thr.get()).Join());
    }
  }

  void WriterThread();
  void ReaderThread();
  void DeleterThread();

 protected:
  // Used to generate random data. All PRNG instances are seeded with this
  // value to ensure that the test is reproducible.
  int rand_seed_;

  // Tells the threads to stop running.
  CountDownLatch stop_latch_;

  // Tracks blocks that have been synced and are ready to be read/deleted.
  vector<BlockId> written_blocks_;

  // Protects written_blocks_.
  rw_spinlock lock_;

  // The block manager.
  gscoped_ptr<BlockManager> bm_;

  // The running threads.
  vector<scoped_refptr<Thread> > threads_;

  // Some performance counters.

  AtomicInt<int64_t> total_blocks_written_;
  AtomicInt<int64_t> total_bytes_written_;

  AtomicInt<int64_t> total_blocks_read_;
  AtomicInt<int64_t> total_bytes_read_;

  AtomicInt<int64_t> total_blocks_deleted_;
};

template <typename T>
void BlockManagerStressTest<T>::WriterThread() {
  string thread_name = Thread::current_thread()->name();
  LOG(INFO) << "Thread " << thread_name << " starting";

  Random rand(rand_seed_);
  size_t num_blocks_written = 0;
  size_t num_bytes_written = 0;
  MonoDelta tight_loop(MonoDelta::FromSeconds(0));
  while (!ShouldStop(tight_loop)) {
    vector<WritableBlock*> dirty_blocks;
    ElementDeleter deleter(&dirty_blocks);
    vector<Random> dirty_block_rands;

    // Create the blocks and write out the PRNG seeds.
    for (int i = 0; i < FLAGS_block_group_size; i++) {
      gscoped_ptr<WritableBlock> block;
      CHECK_OK(bm_->CreateBlock(&block));

      const uint32_t seed = rand.Next() + 1;
      Slice seed_slice(reinterpret_cast<const uint8_t*>(&seed), sizeof(seed));
      LOG(INFO) << "Creating block " << block->id().ToString() << " with seed " << seed;
      CHECK_OK(block->Append(seed_slice));

      dirty_blocks.push_back(block.release());
      dirty_block_rands.push_back(Random(seed));
    }

    // Write a large amount of data to the group of blocks.
    //
    // To emulate a real life workload, we pick the next block to write at
    // random, and write a smaller chunk of data to it.
    LOG(INFO) << "Writing " << FLAGS_block_group_bytes << " bytes into new blocks";
    size_t total_dirty_bytes = 0;
    while (total_dirty_bytes < FLAGS_block_group_bytes) {
      // Pick the next block.
      int next_block_idx = rand.Skewed(log2(dirty_blocks.size()));
      WritableBlock* block = dirty_blocks[next_block_idx];
      Random& rand = dirty_block_rands[next_block_idx];

      // Write a small chunk of data.
      faststring data;
      while (data.length() < FLAGS_num_bytes_per_write) {
        const uint32_t next_int = rand.Next();
        data.append(&next_int, sizeof(next_int));
      }
      CHECK_OK(block->Append(data));
      total_dirty_bytes += data.length();
    }

    // Close all dirty blocks.
    //
    // We could close them implicitly when the blocks are destructed but
    // this way we can check for errors.
    LOG(INFO) << "Closing new blocks";
    CHECK_OK(bm_->CloseBlocks(dirty_blocks));

    // Publish the now sync'ed blocks to readers and deleters.
    {
      lock_guard<rw_spinlock> l(&lock_);
      for (WritableBlock* block : dirty_blocks) {
        written_blocks_.push_back(block->id());
      }
    }
    num_blocks_written += dirty_blocks.size();
    num_bytes_written += total_dirty_bytes;
  }

  LOG(INFO) << Substitute("Thread $0 stopping. Wrote $1 blocks ($2 bytes)",
                          thread_name, num_blocks_written, num_bytes_written);
  total_blocks_written_.IncrementBy(num_blocks_written);
  total_bytes_written_.IncrementBy(num_bytes_written);
}

template <typename T>
void BlockManagerStressTest<T>::ReaderThread() {
  string thread_name = Thread::current_thread()->name();
  LOG(INFO) << "Thread " << thread_name << " starting";

  Random rand(rand_seed_);
  size_t num_blocks_read = 0;
  size_t num_bytes_read = 0;
  MonoDelta tight_loop(MonoDelta::FromSeconds(0));
  while (!ShouldStop(tight_loop)) {
    gscoped_ptr<ReadableBlock> block;
    {
      // Grab a block at random.
      shared_lock<rw_spinlock> l(&lock_);
      size_t num_blocks = written_blocks_.size();
      if (num_blocks > 0) {
        uint32_t next_id = rand.Uniform(num_blocks);
        const BlockId& block_id = written_blocks_[next_id];
        CHECK_OK(bm_->OpenBlock(block_id, &block));
      }
    }
    if (!block) {
      continue;
    }

    // Read it fully into memory.
    string block_id = block->id().ToString();
    uint64_t block_size;
    CHECK_OK(block->Size(&block_size));
    Slice data;
    gscoped_ptr<uint8_t[]> scratch(new uint8_t[block_size]);
    CHECK_OK(block->Read(0, block_size, &data, scratch.get()));
    LOG(INFO) << "Read " << block_size << " bytes from block " << block_id;

    // The first 4 bytes correspond to the PRNG seed.
    CHECK(data.size() >= 4);
    uint32_t seed;
    memcpy(&seed, data.data(), sizeof(uint32_t));
    LOG(INFO) << "Read seed " << seed << " from block " << block_id;
    Random rand(seed);

    // Verify every subsequent number using the PRNG.
    size_t bytes_processed;
    for (bytes_processed = 4; // start after the PRNG seed
        bytes_processed < data.size();
        bytes_processed += sizeof(uint32_t)) {
      uint32_t expected_num = rand.Next();
      uint32_t actual_num;
      memcpy(&actual_num, data.data() + bytes_processed, sizeof(uint32_t));
      if (expected_num != actual_num) {
        LOG(FATAL) << "Read " << actual_num << " and not " << expected_num
                   << " from position " << bytes_processed << " in block "
                   << block_id;
      }
    }
    CHECK_EQ(bytes_processed, data.size());
    LOG(INFO) << "Finished reading block " << block->id().ToString();
    num_blocks_read++;
    num_bytes_read += block_size;
  }

  LOG(INFO) << Substitute("Thread $0 stopping. Read $1 blocks ($2 bytes)",
                          thread_name, num_blocks_read, num_bytes_read);
  total_blocks_read_.IncrementBy(num_blocks_read);
  total_bytes_read_.IncrementBy(num_bytes_read);
}

template <typename T>
void BlockManagerStressTest<T>::DeleterThread() {
  string thread_name = Thread::current_thread()->name();
  LOG(INFO) << "Thread " << thread_name << " starting";

  size_t num_blocks_deleted = 0;
  MonoDelta sleep_time(MonoDelta::FromSeconds(1));
  while (!ShouldStop(sleep_time)) {
    // Grab all the blocks we can.
    vector<BlockId> to_delete;
    {
      lock_guard<rw_spinlock> l(&lock_);
      to_delete.swap(written_blocks_);
    }

    // And delete them.
    for (const BlockId& block_id : to_delete) {
      LOG(INFO) << "Deleting block " << block_id.ToString();
      CHECK_OK(bm_->DeleteBlock(block_id));
    }
    num_blocks_deleted += to_delete.size();
  }

  LOG(INFO) << Substitute("Thread $0 stopping. Deleted $1 blocks",
                          thread_name, num_blocks_deleted);
  total_blocks_deleted_.IncrementBy(num_blocks_deleted);
}

// What kinds of BlockManagers are supported?
#if defined(__linux__)
typedef ::testing::Types<FileBlockManager, LogBlockManager> BlockManagers;
#else
typedef ::testing::Types<FileBlockManager> BlockManagers;
#endif
TYPED_TEST_CASE(BlockManagerStressTest, BlockManagers);

TYPED_TEST(BlockManagerStressTest, StressTest) {
  OverrideFlagForSlowTests("test_duration_secs", "30");
  OverrideFlagForSlowTests("block_group_size", "16");
  OverrideFlagForSlowTests("block_group_bytes",
                           Substitute("$0", 64 * 1024 * 1024));
  OverrideFlagForSlowTests("num_bytes_per_write",
                           Substitute("$0", 64 * 1024));

  if ((FLAGS_block_group_size & (FLAGS_block_group_size - 1)) != 0) {
    LOG(FATAL) << "block_group_size " << FLAGS_block_group_size
               << " is not a power of 2";
  }

  LOG(INFO) << "Running on fresh block manager";
  this->RunTest(FLAGS_test_duration_secs / 2);
  LOG(INFO) << "Running on populated block manager";
  // Blow away old memtrackers before creating new block manager.
  this->bm_.reset();
  this->bm_.reset(this->CreateBlockManager());
  ASSERT_OK(this->bm_->Open());
  this->RunTest(FLAGS_test_duration_secs / 2);

  LOG(INFO) << "Printing test totals";
  LOG(INFO) << "--------------------";
  LOG(INFO) << Substitute("Wrote $0 blocks ($1 bytes) via $2 threads",
                          this->total_blocks_written_.Load(),
                          this->total_bytes_written_.Load(),
                          FLAGS_num_writer_threads);
  LOG(INFO) << Substitute("Read $0 blocks ($1 bytes) via $2 threads",
                          this->total_blocks_read_.Load(),
                          this->total_bytes_read_.Load(),
                          FLAGS_num_reader_threads);
  LOG(INFO) << Substitute("Deleted $0 blocks via $1 threads",
                          this->total_blocks_deleted_.Load(),
                          FLAGS_num_deleter_threads);
}

} // namespace fs
} // namespace kudu
