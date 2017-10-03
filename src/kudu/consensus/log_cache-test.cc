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

#include <gtest/gtest.h>
#include <memory>
#include <string>

#include "kudu/common/wire_protocol-test-util.h"
#include "kudu/consensus/consensus-test-util.h"
#include "kudu/consensus/log.h"
#include "kudu/consensus/log_cache.h"
#include "kudu/fs/fs_manager.h"
#include "kudu/gutil/bind_helpers.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/server/hybrid_clock.h"
#include "kudu/util/mem_tracker.h"
#include "kudu/util/metrics.h"
#include "kudu/util/test_util.h"

using std::shared_ptr;

DECLARE_int32(log_cache_size_limit_mb);
DECLARE_int32(global_log_cache_size_limit_mb);

METRIC_DECLARE_entity(tablet);

namespace kudu {
namespace consensus {

static const char* kPeerUuid = "leader";
static const char* kTestTablet = "test-tablet";

class LogCacheTest : public KuduTest {
 public:
  LogCacheTest()
    : schema_(GetSimpleTestSchema()),
      metric_entity_(METRIC_ENTITY_tablet.Instantiate(&metric_registry_, "LogCacheTest")) {
  }

  virtual void SetUp() OVERRIDE {
    KuduTest::SetUp();
    fs_manager_.reset(new FsManager(env_.get(), GetTestPath("fs_root")));
    ASSERT_OK(fs_manager_->CreateInitialFileSystemLayout());
    ASSERT_OK(fs_manager_->Open());
    CHECK_OK(log::Log::Open(log::LogOptions(),
                            fs_manager_.get(),
                            kTestTablet,
                            schema_,
                            0, // schema_version
                            NULL,
                            &log_));

    CloseAndReopenCache(MinimumOpId());
    clock_.reset(new server::HybridClock());
    ASSERT_OK(clock_->Init());
  }

  virtual void TearDown() OVERRIDE {
    log_->WaitUntilAllFlushed();
  }

  void CloseAndReopenCache(const OpId& preceding_id) {
    // Blow away the memtrackers before creating the new cache.
    cache_.reset();

    cache_.reset(new LogCache(metric_entity_,
                              log_.get(),
                              kPeerUuid,
                              kTestTablet));
    cache_->Init(preceding_id);
  }

 protected:
  static void FatalOnError(const Status& s) {
    CHECK_OK(s);
  }

  Status AppendReplicateMessagesToCache(
    int first,
    int count,
    int payload_size = 0) {

    for (int i = first; i < first + count; i++) {
      int term = i / 7;
      int index = i;
      vector<ReplicateRefPtr> msgs;
      msgs.push_back(make_scoped_refptr_replicate(
                       CreateDummyReplicate(term, index, clock_->Now(), payload_size).release()));
      RETURN_NOT_OK(cache_->AppendOperations(msgs, Bind(&FatalOnError)));
    }
    return Status::OK();
  }

  const Schema schema_;
  MetricRegistry metric_registry_;
  scoped_refptr<MetricEntity> metric_entity_;
  gscoped_ptr<FsManager> fs_manager_;
  gscoped_ptr<LogCache> cache_;
  scoped_refptr<log::Log> log_;
  scoped_refptr<server::Clock> clock_;
};


TEST_F(LogCacheTest, TestAppendAndGetMessages) {
  ASSERT_EQ(0, cache_->metrics_.log_cache_num_ops->value());
  ASSERT_EQ(0, cache_->metrics_.log_cache_size->value());
  ASSERT_OK(AppendReplicateMessagesToCache(1, 100));
  ASSERT_EQ(100, cache_->metrics_.log_cache_num_ops->value());
  ASSERT_GE(cache_->metrics_.log_cache_size->value(), 500);
  log_->WaitUntilAllFlushed();

  vector<ReplicateRefPtr> messages;
  OpId preceding;
  ASSERT_OK(cache_->ReadOps(0, 8 * 1024 * 1024, &messages, &preceding));
  EXPECT_EQ(100, messages.size());
  EXPECT_EQ("0.0", OpIdToString(preceding));

  // Get starting in the middle of the cache.
  messages.clear();
  ASSERT_OK(cache_->ReadOps(70, 8 * 1024 * 1024, &messages, &preceding));
  EXPECT_EQ(30, messages.size());
  EXPECT_EQ("10.70", OpIdToString(preceding));
  EXPECT_EQ("10.71", OpIdToString(messages[0]->get()->id()));

  // Get at the end of the cache
  messages.clear();
  ASSERT_OK(cache_->ReadOps(100, 8 * 1024 * 1024, &messages, &preceding));
  EXPECT_EQ(0, messages.size());
  EXPECT_EQ("14.100", OpIdToString(preceding));

  // Evict some and verify that the eviction took effect.
  cache_->EvictThroughOp(50);
  ASSERT_EQ(50, cache_->metrics_.log_cache_num_ops->value());

  // Can still read data that was evicted, since it got written through.
  messages.clear();
  ASSERT_OK(cache_->ReadOps(20, 8 * 1024 * 1024, &messages, &preceding));
  EXPECT_EQ(80, messages.size());
  EXPECT_EQ("2.20", OpIdToString(preceding));
  EXPECT_EQ("3.21", OpIdToString(messages[0]->get()->id()));
}


// Ensure that the cache always yields at least one message,
// even if that message is larger than the batch size. This ensures
// that we don't get "stuck" in the case that a large message enters
// the cache.
TEST_F(LogCacheTest, TestAlwaysYieldsAtLeastOneMessage) {
  // generate a 2MB dummy payload
  const int kPayloadSize = 2 * 1024 * 1024;

  // Append several large ops to the cache
  ASSERT_OK(AppendReplicateMessagesToCache(1, 4, kPayloadSize));
  log_->WaitUntilAllFlushed();

  // We should get one of them, even though we only ask for 100 bytes
  vector<ReplicateRefPtr> messages;
  OpId preceding;
  ASSERT_OK(cache_->ReadOps(0, 100, &messages, &preceding));
  ASSERT_EQ(1, messages.size());
}

// Tests that the cache returns Status::NotFound() if queried for messages after an
// index that is higher than it's latest, returns an empty set of messages when queried for
// the the last index and returns all messages when queried for MinimumOpId().
TEST_F(LogCacheTest, TestCacheEdgeCases) {
  // Append 1 message to the cache
  ASSERT_OK(AppendReplicateMessagesToCache(1, 1));
  log_->WaitUntilAllFlushed();

  std::vector<ReplicateRefPtr> messages;
  OpId preceding;

  // Test when the searched index is MinimumOpId().index().
  ASSERT_OK(cache_->ReadOps(0, 100, &messages, &preceding));
  ASSERT_EQ(1, messages.size());
  ASSERT_OPID_EQ(MakeOpId(0, 0), preceding);

  messages.clear();
  preceding.Clear();
  // Test when 'after_op_index' is the last index in the cache.
  ASSERT_OK(cache_->ReadOps(1, 100, &messages, &preceding));
  ASSERT_EQ(0, messages.size());
  ASSERT_OPID_EQ(MakeOpId(0, 1), preceding);

  messages.clear();
  preceding.Clear();
  // Now test the case when 'after_op_index' is after the last index
  // in the cache.
  Status s = cache_->ReadOps(2, 100, &messages, &preceding);
  ASSERT_TRUE(s.IsIncomplete()) << "unexpected status: " << s.ToString();
  ASSERT_EQ(0, messages.size());
  ASSERT_FALSE(preceding.IsInitialized());

  messages.clear();
  preceding.Clear();

  // Evict entries from the cache, and ensure that we can still read
  // entries at the beginning of the log.
  cache_->EvictThroughOp(50);
  ASSERT_OK(cache_->ReadOps(0, 100, &messages, &preceding));
  ASSERT_EQ(1, messages.size());
  ASSERT_OPID_EQ(MakeOpId(0, 0), preceding);
}


TEST_F(LogCacheTest, TestMemoryLimit) {
  FLAGS_log_cache_size_limit_mb = 1;
  CloseAndReopenCache(MinimumOpId());

  const int kPayloadSize = 400 * 1024;
  // Limit should not be violated.
  ASSERT_OK(AppendReplicateMessagesToCache(1, 1, kPayloadSize));
  log_->WaitUntilAllFlushed();
  ASSERT_EQ(1, cache_->num_cached_ops());

  // Verify the size is right. It's not exactly kPayloadSize because of in-memory
  // overhead, etc.
  int size_with_one_msg = cache_->BytesUsed();
  ASSERT_GT(size_with_one_msg, 300 * 1024);
  ASSERT_LT(size_with_one_msg, 500 * 1024);

  // Add another operation which fits under the 1MB limit.
  ASSERT_OK(AppendReplicateMessagesToCache(2, 1, kPayloadSize));
  log_->WaitUntilAllFlushed();
  ASSERT_EQ(2, cache_->num_cached_ops());

  int size_with_two_msgs = cache_->BytesUsed();
  ASSERT_GT(size_with_two_msgs, 2 * 300 * 1024);
  ASSERT_LT(size_with_two_msgs, 2 * 500 * 1024);

  // Append a third operation, which will push the cache size above the 1MB limit
  // and cause eviction of the first operation.
  LOG(INFO) << "appending op 3";
  // Verify that we have trimmed by appending a message that would
  // otherwise be rejected, since the cache max size limit is 2MB.
  ASSERT_OK(AppendReplicateMessagesToCache(3, 1, kPayloadSize));
  log_->WaitUntilAllFlushed();
  ASSERT_EQ(2, cache_->num_cached_ops());
  ASSERT_EQ(size_with_two_msgs, cache_->BytesUsed());

  // Test explicitly evicting one of the ops.
  cache_->EvictThroughOp(2);
  ASSERT_EQ(1, cache_->num_cached_ops());
  ASSERT_EQ(size_with_one_msg, cache_->BytesUsed());

  // Explicitly evict the last op.
  cache_->EvictThroughOp(3);
  ASSERT_EQ(0, cache_->num_cached_ops());
  ASSERT_EQ(cache_->BytesUsed(), 0);
}

TEST_F(LogCacheTest, TestGlobalMemoryLimit) {
  FLAGS_global_log_cache_size_limit_mb = 4;
  CloseAndReopenCache(MinimumOpId());

  // Exceed the global hard limit.
  ScopedTrackedConsumption consumption(cache_->parent_tracker_, 3*1024*1024);

  const int kPayloadSize = 768 * 1024;

  // Should succeed, but only end up caching one of the two ops because of the global limit.
  ASSERT_OK(AppendReplicateMessagesToCache(1, 2, kPayloadSize));
  log_->WaitUntilAllFlushed();

  ASSERT_EQ(1, cache_->num_cached_ops());
  ASSERT_LE(cache_->BytesUsed(), 1024 * 1024);
}

// Test that the log cache properly replaces messages when an index
// is reused. This is a regression test for a bug where the memtracker's
// consumption wasn't properly managed when messages were replaced.
TEST_F(LogCacheTest, TestReplaceMessages) {
  const int kPayloadSize = 128 * 1024;
  shared_ptr<MemTracker> tracker = cache_->tracker_;;
  ASSERT_EQ(0, tracker->consumption());

  ASSERT_OK(AppendReplicateMessagesToCache(1, 1, kPayloadSize));
  int size_with_one_msg = tracker->consumption();

  for (int i = 0; i < 10; i++) {
    ASSERT_OK(AppendReplicateMessagesToCache(1, 1, kPayloadSize));
  }

  log_->WaitUntilAllFlushed();

  EXPECT_EQ(size_with_one_msg, tracker->consumption());
  EXPECT_EQ(Substitute("Pinned index: 2, LogCacheStats(num_ops=1, bytes=$0)",
                       size_with_one_msg),
            cache_->ToString());
}

} // namespace consensus
} // namespace kudu
