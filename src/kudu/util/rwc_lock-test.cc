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

#include <boost/thread/thread.hpp>
#include <string>
#include <vector>

#include "kudu/gutil/atomicops.h"
#include "kudu/util/rwc_lock.h"
#include "kudu/util/test_util.h"
#include "kudu/util/locks.h"

namespace kudu {

using base::subtle::NoBarrier_Load;
using base::subtle::Release_Store;
using std::string;
using std::vector;

class RWCLockTest : public KuduTest {};

// Holds counters of how many threads hold the lock in each of the
// provided modes.
struct LockHoldersCount {
  LockHoldersCount()
    : num_readers(0),
      num_writers(0),
      num_committers(0) {
  }

  // Check the invariants of the lock counts.
  void CheckInvariants() {
    // At no time should we have more than one writer or committer.
    CHECK_LE(num_writers, 1);
    CHECK_LE(num_committers, 1);

    // If we have any readers, then we should not have any committers.
    if (num_readers > 0) {
      CHECK_EQ(num_committers, 0);
    }
  }

  void AdjustReaders(int delta) {
    boost::lock_guard<simple_spinlock> l(lock);
    num_readers += delta;
    CheckInvariants();
  }

  void AdjustWriters(int delta) {
    boost::lock_guard<simple_spinlock> l(lock);
    num_writers += delta;
    CheckInvariants();
  }

  void AdjustCommitters(int delta) {
    boost::lock_guard<simple_spinlock> l(lock);
    num_committers += delta;
    CheckInvariants();
  }

  int num_readers;
  int num_writers;
  int num_committers;
  simple_spinlock lock;
};

struct SharedState {
  LockHoldersCount counts;
  RWCLock rwc_lock;
  Atomic32 stop;
};

void ReaderThread(SharedState* state) {
  while (!NoBarrier_Load(&state->stop)) {
    state->rwc_lock.ReadLock();
    state->counts.AdjustReaders(1);
    state->counts.AdjustReaders(-1);
    state->rwc_lock.ReadUnlock();
  }
}

void WriterThread(SharedState* state) {
  string local_str;
  while (!NoBarrier_Load(&state->stop)) {
    state->rwc_lock.WriteLock();
    state->counts.AdjustWriters(1);

    state->rwc_lock.UpgradeToCommitLock();
    state->counts.AdjustWriters(-1);
    state->counts.AdjustCommitters(1);

    state->counts.AdjustCommitters(-1);
    state->rwc_lock.CommitUnlock();
  }
}


TEST_F(RWCLockTest, TestCorrectBehavior) {
  SharedState state;
  Release_Store(&state.stop, 0);

  vector<boost::thread*> threads;

  const int kNumWriters = 5;
  const int kNumReaders = 5;

  for (int i = 0; i < kNumWriters; i++) {
    threads.push_back(new boost::thread(WriterThread, &state));
  }
  for (int i = 0; i < kNumReaders; i++) {
    threads.push_back(new boost::thread(ReaderThread, &state));
  }

  if (AllowSlowTests()) {
    SleepFor(MonoDelta::FromSeconds(1));
  } else {
    SleepFor(MonoDelta::FromMilliseconds(100));
  }

  Release_Store(&state.stop, 1);

  for (boost::thread* t : threads) {
    t->join();
    delete t;
  }

}

} // namespace kudu
