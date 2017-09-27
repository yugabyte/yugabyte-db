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

#ifndef YB_TABLET_PREPARE_THREAD_H
#define YB_TABLET_PREPARE_THREAD_H

#include <gflags/gflags.h>

#include "yb/util/status.h"

DECLARE_int32(max_group_replicate_batch_size);
DECLARE_int32(prepare_queue_max_size);

namespace yb {

namespace consensus {
class Consensus;
}

namespace tablet {

class OperationDriver;

class PrepareThreadImpl;

// This is a thread that invokes the "prepare" step on single-shard transactions and, for
// leader-side transactions, submits them for replication to the consensus in batches. This is
// useful because we have a "fat lock" in the consensus.
class PrepareThread {
 public:
  explicit PrepareThread(consensus::Consensus* consensus);
  ~PrepareThread();

  CHECKED_STATUS Start();
  void Stop();

  CHECKED_STATUS Submit(OperationDriver* txn_driver);

 private:
  std::unique_ptr<PrepareThreadImpl> impl_;
};

};  // namespace tablet
}  // namespace yb
#endif  // YB_TABLET_PREPARE_THREAD_H
