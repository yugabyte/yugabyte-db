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

#include "yb/tablet/local_tablet_writer.h"
#include "yb/tablet/tablet.h"

#include "yb/common/ql_protocol_util.h"

namespace yb {
namespace tablet {

namespace {

// This is used for providing OpIds to write operations, which must always be increasing.
class AutoIncrementingCounter {
 public:
  AutoIncrementingCounter() : next_index_(1) {}
  int64_t GetAndIncrement() { return next_index_.fetch_add(1); }
 private:
  std::atomic<int64_t> next_index_;
};

} // namespace

LocalTabletWriter::LocalTabletWriter(Tablet* tablet) : tablet_(tablet) {
}

// Perform a write against the local tablet.
// Returns a bad Status if the applied operation had a per-row error.
Status LocalTabletWriter::Write(QLWriteRequestPB* request) {
  Batch batch;
  batch.Add()->Swap(request);
  return WriteBatch(&batch);
}

Status LocalTabletWriter::WriteBatch(Batch* batch) {
  for (auto& req : *batch) {
    req.set_schema_version(tablet_->metadata()->schema_version());
    QLSetHashCode(&req);
  }
  req_.mutable_ql_write_batch()->Swap(batch);

  tx_state_.reset(new WriteOperationState(tablet_, &req_, &resp_));
  HybridTime read_ht;
  RETURN_NOT_OK(tablet_->AcquireLocksAndPerformDocOperations(
      MonoTime::Max() /* deadline */, tx_state_.get(), &read_ht));
  tablet_->StartOperation(tx_state_.get());

  // Create a "fake" OpId and set it in the OperationState for anchoring.
  tx_state_->mutable_op_id()->set_term(0);
  tx_state_->mutable_op_id()->set_index(
      Singleton<AutoIncrementingCounter>::get()->GetAndIncrement());

  tablet_->ApplyRowOperations(tx_state_.get());

  tx_state_->Commit();
  tx_state_->ReleaseDocDbLocks(tablet_);

  // Return the status of first failed op.
  int op_idx = 0;
  for (const auto& result : resp_.ql_response_batch()) {
    if (result.status() != QLResponsePB::YQL_STATUS_OK) {
      return STATUS_FORMAT(RuntimeError, "Op $0 failed: $1 ($2)", op_idx, result.error_message(),
                           QLResponsePB_QLStatus_Name(result.status()));
    }
    op_idx++;
  }
  return Status::OK();
}

}  // namespace tablet
}  // namespace yb
