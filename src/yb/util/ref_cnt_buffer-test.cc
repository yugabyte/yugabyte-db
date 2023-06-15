//
// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
//

#include <condition_variable>
#include <mutex>
#include <thread>

#include <boost/ptr_container/ptr_vector.hpp>
#include <gtest/gtest.h>

#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/test_util.h"

using namespace std::literals;

namespace yb {
namespace util {

class RefCntBufferTest : public YBTest {
};

const size_t kSizeLimit = 0x1000;

// Test buffer allocation by its size. Also check copy semantics.
TEST_F(RefCntBufferTest, TestSize) {
  unsigned int seed = SeedRandom();
  for (auto i = 10000; i--;) {
    size_t size = rand_r(&seed) % (kSizeLimit + 1); // Zero size is also allowed
    RefCntBuffer buffer(size);
    auto copy = buffer;
    for (size_t index = 0; index != size; ++index) {
      buffer.begin()[index] = index;
    }
    ASSERT_EQ(buffer.begin(), copy.begin());
    ASSERT_EQ(buffer.end(), copy.end());
    ASSERT_EQ(buffer.size(), copy.size());
  }
}

// Test buffer allocation by data block.
TEST_F(RefCntBufferTest, TestFromData) {
  unsigned int seed = SeedRandom();
  for (auto i = 10000; i--;) {
    size_t size = rand_r(&seed) % (kSizeLimit + 1); // Zero size is also allowed
    RefCntBuffer buffer(size);
    for (size_t index = 0; index != size; ++index) {
      buffer.begin()[index] = index;
    }

    RefCntBuffer copy(buffer.begin(), buffer.end());
    ASSERT_NE(buffer.begin(), copy.begin());
    ASSERT_NE(buffer.end(), copy.end());
    ASSERT_EQ(buffer.size(), copy.size());
    for (size_t index = 0; index != size; ++index) {
      ASSERT_EQ(buffer.begin()[index], copy.begin()[index]);
    }
  }
}

// Test vector of buffers.
TEST_F(RefCntBufferTest, TestVector) {
  std::vector<RefCntBuffer> v;
  for (auto i = 10000; i--;) {
    v.emplace_back(kSizeLimit);
    ASSERT_TRUE(v.back());
  }

  unsigned int seed = SeedRandom();
  while (!v.empty()) {
    size_t idx = rand_r(&seed) % v.size();
    auto temp = v[idx];
    v[idx] = v.back();
    v.pop_back();
    ASSERT_TRUE(temp);
  }
}

namespace {

const size_t kInitialBuffers = 1000;

class TestQueue {
 public:
  TestQueue(const TestQueue&) = delete;
  TestQueue& operator=(const TestQueue&) = delete;

  TestQueue() {}

  void TalkTo(boost::ptr_vector<TestQueue>* queues) {
    queues_ = queues;
  }

  void Enqueue(RefCntBuffer buffer) {
    {
      std::lock_guard lock(mutex_);
      ASSERT_TRUE(buffer);
      // We don't use std::move in this test because we want to check reference counting.
      buffers_.push_back(buffer);
      ++received_buffers_;
    }
    cond_.notify_one();
  }

  void Interrupt() {
    {
      std::lock_guard lock(mutex_);
      interruption_requested_ = true;
    }
    cond_.notify_one();
  }

  void Assert() {
    LOG(INFO) << "Sent buffers: " << sent_buffers_ << ", received buffers: " << received_buffers_
              << ", has buffers: " << buffers_.size();
    ASSERT_EQ(kInitialBuffers + received_buffers_ - sent_buffers_, buffers_.size());
  }

  void Run() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (auto i = kInitialBuffers; i--;) {
      buffers_.emplace_back(kSizeLimit);
      ASSERT_TRUE(buffers_.back());
    }

    unsigned int seed = SeedRandom();
    while (!interruption_requested_) {
      RefCntBuffer buffer;
      if (!buffers_.empty()) {
        size_t idx = rand_r(&seed) % buffers_.size();
        buffer = buffers_[idx];
        buffers_[idx] = buffers_.back();
        buffers_.pop_back();
        ++sent_buffers_;
        ASSERT_TRUE(buffer);
      }

      if (buffer) {
        lock.unlock();
        size_t queue_index = rand_r(&seed) % queues_->size();
        (*queues_)[queue_index].Enqueue(buffer);
        lock.lock();
      }

      cond_.wait_for(lock, 1ms); // Wait until something enqueued, or timeout.
    }
  }
 private:
  boost::ptr_vector<TestQueue>* queues_;
  std::vector<RefCntBuffer> buffers_;
  std::atomic<bool> interruption_requested_ = {false};
  std::mutex mutex_;
  std::condition_variable cond_;
  size_t sent_buffers_ = 0;
  size_t received_buffers_ = 0;
};

} // namespace

// Test how buffers behave with multiple threads. Mostly for ASAN and TSAN.
TEST_F(RefCntBufferTest, TestThreads) {
  const size_t kQueuesCount = 4;
  boost::ptr_vector<TestQueue> queues;
  for (size_t i = kQueuesCount; i--;) {
    queues.push_back(new TestQueue);
  }
  for (auto& queue : queues) {
    queue.TalkTo(&queues);
  }

  std::vector<std::thread> threads;
  for (auto& queue : queues) {
    threads.emplace_back(std::bind(&TestQueue::Run, &queue));
  }

  std::this_thread::sleep_for(2s);

  for (auto& queue : queues) {
    queue.Interrupt();
  }

  for (auto& thread : threads) {
    thread.join();
  }

  for (auto& queue : queues) {
    queue.Assert();
  }
}

} // namespace util
} // namespace yb
