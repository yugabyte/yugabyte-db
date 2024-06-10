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

#pragma once

#include <future>

#include "yb/client/client_fwd.h"
#include "yb/rpc/rpc_fwd.h"
#include "yb/server/server_fwd.h"
#include "yb/tablet/tablet_fwd.h"
#include "yb/util/monotime.h"

namespace yb {
namespace tablet {

class TransactionParticipantContext {
 public:
  virtual const std::string& permanent_uuid() const = 0;
  virtual const std::string& tablet_id() const = 0;
  virtual const std::shared_future<client::YBClient*>& client_future() const = 0;
  virtual Result<client::YBClient*> client() const = 0;
  virtual const server::ClockPtr& clock_ptr() const = 0;
  virtual rpc::Scheduler& scheduler() const = 0;

  // Fills RemoveIntentsData with information about replicated state.
  virtual Status GetLastReplicatedData(RemoveIntentsData* data) = 0;

  // Enqueue task to participant context strand.
  virtual void StrandEnqueue(rpc::StrandTask* task) = 0;
  virtual void UpdateClock(HybridTime hybrid_time) = 0;
  virtual bool IsLeader() = 0;
  virtual Status SubmitUpdateTransaction(
      std::unique_ptr<UpdateTxnOperation> state, int64_t term) = 0;

  // Returns hybrid time that lower than any future transaction apply record.
  virtual HybridTime SafeTimeForTransactionParticipant() = 0;

  virtual Result<HybridTime> WaitForSafeTime(HybridTime safe_time, CoarseTimePoint deadline) = 0;

  std::string LogPrefix() const;

  HybridTime Now();

 protected:
  ~TransactionParticipantContext() {}
};

} // namespace tablet
} // namespace yb
