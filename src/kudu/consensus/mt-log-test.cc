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

#include "kudu/consensus/log-test-base.h"

#include <boost/thread/locks.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>

#include <algorithm>
#include <vector>

#include "kudu/consensus/log_index.h"
#include "kudu/gutil/algorithm.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/locks.h"
#include "kudu/util/random.h"
#include "kudu/util/thread.h"

DEFINE_int32(num_writer_threads, 4, "Number of threads writing to the log");
DEFINE_int32(num_batches_per_thread, 2000, "Number of batches per thread");
DEFINE_int32(num_ops_per_batch_avg, 5, "Target average number of ops per batch");

namespace kudu {
namespace log {

using std::vector;
using consensus::ReplicateRefPtr;
using consensus::make_scoped_refptr_replicate;

namespace {

class CustomLatchCallback : public RefCountedThreadSafe<CustomLatchCallback> {
 public:
  CustomLatchCallback(CountDownLatch* latch, vector<Status>* errors)
      : latch_(latch),
        errors_(errors) {
  }

  void StatusCB(const Status& s) {
    if (!s.ok()) {
      errors_->push_back(s);
    }
    latch_->CountDown();
  }

  StatusCallback AsStatusCallback() {
    return Bind(&CustomLatchCallback::StatusCB, this);
  }

 private:
  CountDownLatch* latch_;
  vector<Status>* errors_;
};

} // anonymous namespace

extern const char *kTestTablet;

class MultiThreadedLogTest : public LogTestBase {
 public:
  MultiThreadedLogTest()
      : random_(SeedRandom()) {
  }

  virtual void SetUp() OVERRIDE {
    LogTestBase::SetUp();
  }

  void LogWriterThread(int thread_id) {
    CountDownLatch latch(FLAGS_num_batches_per_thread);
    vector<Status> errors;
    for (int i = 0; i < FLAGS_num_batches_per_thread; i++) {
      LogEntryBatch* entry_batch;
      vector<consensus::ReplicateRefPtr> batch_replicates;
      int num_ops = static_cast<int>(random_.Normal(
          static_cast<double>(FLAGS_num_ops_per_batch_avg), 1.0));
      DVLOG(1) << num_ops << " ops in this batch";
      num_ops =  std::max(num_ops, 1);
      {
        boost::lock_guard<simple_spinlock> lock_guard(lock_);
        for (int j = 0; j < num_ops; j++) {
          ReplicateRefPtr replicate = make_scoped_refptr_replicate(new ReplicateMsg);
          int32_t index = current_index_++;
          OpId* op_id = replicate->get()->mutable_id();
          op_id->set_term(0);
          op_id->set_index(index);

          replicate->get()->set_op_type(WRITE_OP);
          replicate->get()->set_timestamp(clock_->Now().ToUint64());

          tserver::WriteRequestPB* request = replicate->get()->mutable_write_request();
          AddTestRowToPB(RowOperationsPB::INSERT, schema_, index, 0,
                         "this is a test insert",
                         request->mutable_row_operations());
          request->set_tablet_id(kTestTablet);
          batch_replicates.push_back(replicate);
        }

        gscoped_ptr<log::LogEntryBatchPB> entry_batch_pb;
        CreateBatchFromAllocatedOperations(batch_replicates,
                                           &entry_batch_pb);

        ASSERT_OK(log_->Reserve(REPLICATE, entry_batch_pb.Pass(), &entry_batch));
      } // lock_guard scope
      auto cb = new CustomLatchCallback(&latch, &errors);
      entry_batch->SetReplicates(batch_replicates);
      ASSERT_OK(log_->AsyncAppend(entry_batch, cb->AsStatusCallback()));
    }
    LOG_TIMING(INFO, strings::Substitute("thread $0 waiting to append and sync $1 batches",
                                        thread_id, FLAGS_num_batches_per_thread)) {
      latch.Wait();
    }
    for (const Status& status : errors) {
      WARN_NOT_OK(status, "Unexpected failure during AsyncAppend");
    }
    ASSERT_EQ(0, errors.size());
  }

  void Run() {
    for (int i = 0; i < FLAGS_num_writer_threads; i++) {
      scoped_refptr<kudu::Thread> new_thread;
      CHECK_OK(kudu::Thread::Create("test", "inserter",
          &MultiThreadedLogTest::LogWriterThread, this, i, &new_thread));
      threads_.push_back(new_thread);
    }
    for (scoped_refptr<kudu::Thread>& thread : threads_) {
      ASSERT_OK(ThreadJoiner(thread.get()).Join());
    }
  }
 private:
  ThreadSafeRandom random_;
  simple_spinlock lock_;
  vector<scoped_refptr<kudu::Thread> > threads_;
};

TEST_F(MultiThreadedLogTest, TestAppends) {
  BuildLog();
  int start_current_id = current_index_;
  LOG_TIMING(INFO, strings::Substitute("inserting $0 batches($1 threads, $2 per-thread)",
                                      FLAGS_num_writer_threads * FLAGS_num_batches_per_thread,
                                      FLAGS_num_batches_per_thread, FLAGS_num_writer_threads)) {
    ASSERT_NO_FATAL_FAILURE(Run());
  }
  ASSERT_OK(log_->Close());

  gscoped_ptr<LogReader> reader;
  ASSERT_OK(LogReader::Open(fs_manager_.get(), NULL, kTestTablet, NULL, &reader));
  SegmentSequence segments;
  ASSERT_OK(reader->GetSegmentsSnapshot(&segments));

  for (const SegmentSequence::value_type& entry : segments) {
    ASSERT_OK(entry->ReadEntries(&entries_));
  }
  vector<uint32_t> ids;
  EntriesToIdList(&ids);
  DVLOG(1) << "Wrote total of " << current_index_ - start_current_id << " ops";
  ASSERT_EQ(current_index_ - start_current_id, ids.size());
  ASSERT_TRUE(std::is_sorted(ids.begin(), ids.end()));
}

} // namespace log
} // namespace kudu
