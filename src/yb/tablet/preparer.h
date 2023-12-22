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

#pragma once

#include "yb/util/flags.h"

#include "yb/util/status_fwd.h"
#include "yb/util/threadpool.h"

DECLARE_int32(prepare_queue_max_size);

namespace yb {
class ThreadPool;

namespace consensus {
class Consensus;
}

namespace tablet {

class OperationDriver;

class PreparerImpl;

// This is a thread that invokes the "prepare" step on single-shard transactions and, for
// leader-side transactions, submits them for replication to the consensus in batches. This is
// useful because we have a "fat lock" in the consensus.
// Preparer does not manage a thread but only submits to a token in a thread pool.
class Preparer {
 public:
  explicit Preparer(consensus::Consensus* consensus, ThreadPool* tablet_prepare_pool);
  ~Preparer();

  Status Start();
  void Stop();

  Status Submit(OperationDriver* txn_driver);
  ThreadPoolToken* PoolToken();

  void DumpStatusHtml(std::ostream& out);

 private:
  std::unique_ptr<PreparerImpl> impl_;
};

};  // namespace tablet
}  // namespace yb
