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

#include "yb/common/ql_protocol_util.h"
#include "yb/common/ql_value.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/singleton.h"

#include "yb/tablet/operations/write_operation.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/write_query.h"

#include "yb/tserver/tserver.pb.h"

#include "yb/util/status_format.h"
#include "yb/util/status_log.h"

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

LocalTabletWriter::LocalTabletWriter(TabletPtr tablet)
    : tablet_(tablet), req_(std::make_unique<tserver::WriteRequestPB>()),
      resp_(std::make_unique<tserver::WriteResponsePB>()) {
}

LocalTabletWriter::~LocalTabletWriter() = default;

// Perform a write against the local tablet.
// Returns a bad Status if the applied operation had a per-row error.
Status LocalTabletWriter::Write(QLWriteRequestPB* request) {
  Batch batch;
  batch.Add()->Swap(request);
  return WriteBatch(&batch);
}

Status LocalTabletWriter::WriteBatch(Batch* batch) {
  req_->Clear();
  for (auto& req : *batch) {
    req.set_schema_version(tablet_->metadata()->schema_version());
    QLSetHashCode(&req);
  }
  req_->mutable_ql_write_batch()->Swap(batch);

  auto query = std::make_unique<WriteQuery>(
      OpId::kUnknownTerm, CoarseTimePoint::max() /* deadline */, this,
      tablet_, nullptr, resp_.get());
  query->set_client_request(*req_);
  write_promise_ = std::promise<Status>();
  tablet_->AcquireLocksAndPerformDocOperations(std::move(query));

  return write_promise_.get_future().get();
}

void LocalTabletWriter::Submit(std::unique_ptr<Operation> operation, int64_t term) {
  auto state = down_cast<WriteOperation*>(operation.get());
  OpId op_id(term, Singleton<AutoIncrementingCounter>::get()->GetAndIncrement());

  auto hybrid_time = tablet_->mvcc_manager()->AddLeaderPending(op_id);
  state->set_hybrid_time(hybrid_time);

  // Create a "fake" OpId and set it in the Operation for anchoring.
  state->set_op_id(op_id);

  CHECK_OK(tablet_->ApplyRowOperations(state));

  tablet_->mvcc_manager()->Replicated(hybrid_time, op_id);

  state->CompleteWithStatus(Status::OK());

  // Return the status of first failed op.
  int op_idx = 0;
  for (const auto& result : resp_->ql_response_batch()) {
    if (result.status() != QLResponsePB::YQL_STATUS_OK) {
      write_promise_.set_value(STATUS_FORMAT(
          RuntimeError, "Op $0 failed: $1 ($2)", op_idx, result.error_message(),
          QLResponsePB_QLStatus_Name(result.status())));
    }
    op_idx++;
  }

  write_promise_.set_value(Status::OK());
}

Result<HybridTime> LocalTabletWriter::ReportReadRestart() {
  return HybridTime();
}

}  // namespace tablet
}  // namespace yb
