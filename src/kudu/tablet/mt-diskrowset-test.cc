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

#include <boost/ptr_container/ptr_vector.hpp>
#include <boost/thread/thread.hpp>
#include <memory>

#include "kudu/tablet/diskrowset-test-base.h"

DEFINE_int32(num_threads, 2, "Number of threads to test");

using std::shared_ptr;
using std::unordered_set;

namespace kudu {
namespace tablet {

class TestMultiThreadedRowSet : public TestRowSet {
 public:
  void RowSetUpdateThread(DiskRowSet *rs) {
    unordered_set<uint32_t> updated;
    UpdateExistingRows(rs, 0.5f, &updated);
  }

  void FlushThread(DiskRowSet *rs) {
    for (int i = 0; i < 10; i++) {
      CHECK_OK(rs->FlushDeltas());
    }
  }

  void StartUpdaterThreads(boost::ptr_vector<boost::thread> *threads,
                           DiskRowSet *rs,
                           int n_threads) {
    for (int i = 0; i < n_threads; i++) {
      threads->push_back(new boost::thread(
                           &TestMultiThreadedRowSet::RowSetUpdateThread, this,
                           rs));
    }
  }

  void StartFlushThread(boost::ptr_vector<boost::thread> *threads,
                        DiskRowSet *rs) {
    threads->push_back(new boost::thread(
                         &TestMultiThreadedRowSet::FlushThread, this, rs));
  }

  void JoinThreads(boost::ptr_vector<boost::thread> *threads) {
    for (boost::thread &thr : *threads) {
      thr.join();
    }
  }
};


TEST_F(TestMultiThreadedRowSet, TestMTUpdate) {
  if (2 == FLAGS_num_threads) {
    if (AllowSlowTests()) {
      FLAGS_num_threads = 16;
    }
  }

  WriteTestRowSet();

  // Re-open the rowset
  shared_ptr<DiskRowSet> rs;
  ASSERT_OK(OpenTestRowSet(&rs));

  // Spawn a bunch of threads, each of which will do updates.
  boost::ptr_vector<boost::thread> threads;
  StartUpdaterThreads(&threads, rs.get(), FLAGS_num_threads);

  JoinThreads(&threads);
}

TEST_F(TestMultiThreadedRowSet, TestMTUpdateAndFlush) {
  if (2 == FLAGS_num_threads) {
    if (AllowSlowTests()) {
      FLAGS_num_threads = 16;
    }
  }

  WriteTestRowSet();

  // Re-open the rowset
  shared_ptr<DiskRowSet> rs;
  ASSERT_OK(OpenTestRowSet(&rs));

  // Spawn a bunch of threads, each of which will do updates.
  boost::ptr_vector<boost::thread> threads;
  StartUpdaterThreads(&threads, rs.get(), FLAGS_num_threads);
  StartFlushThread(&threads, rs.get());

  JoinThreads(&threads);

  // TODO: test that updates were successful -- collect the updated
  // row lists from all the threads, and verify them.
}

} // namespace tablet
} // namespace kudu
