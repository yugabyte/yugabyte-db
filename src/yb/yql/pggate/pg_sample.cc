//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pg_sample.h"

#include <memory>
#include <string>
#include <vector>
#include <utility>

#include "yb/gutil/casts.h"

namespace yb::pggate {

// Internal class to work as the secondary_index_query_ to select sample tuples.
// Like index, it produces ybctids of random records and outer PgSample fetches them.
// Unlike index, it does not use a secondary index, but scans main table instead.
class PgSamplePicker : public PgSelectIndex {
 public:
  PgSamplePicker(
      PgSession::ScopedRefPtr pg_session, const PgObjectId& table_id, bool is_region_local)
      : PgSelectIndex(std::move(pg_session), table_id, is_region_local) {}

  Status Prepare() override {
    target_ = PgTable(VERIFY_RESULT(LoadTable()));
    bind_ = PgTable(nullptr);
    auto read_op = ArenaMakeShared<PgsqlReadOp>(
        arena_ptr(), &arena(), *target_, is_region_local_,
        pg_session_->metrics().metrics_capture());
    read_req_ = std::shared_ptr<LWPgsqlReadRequestPB>(read_op, &read_op->read_request());
    doc_op_ = std::make_shared<PgDocReadOp>(pg_session_, &target_, std::move(read_op));
    return Status::OK();
  }

  Status PrepareSamplingState(
      int targrows, double rstate_w, uint64_t rand_state_s0, uint64_t rand_state_s1) {
    auto* sampling_state = read_req_->mutable_sampling_state();
    sampling_state->set_targrows(targrows);      // target sample size
    sampling_state->set_numrows(0);              // current number of rows selected
    sampling_state->set_samplerows(0);           // rows scanned so far
    sampling_state->set_rowstoskip(-1);          // rows to skip before selecting another
    sampling_state->set_rstate_w(rstate_w);      // Vitter algorithm's W
    auto* rand_state = sampling_state->mutable_rand_state();
    rand_state->set_s0(rand_state_s0);
    rand_state->set_s1(rand_state_s1);
    reservoir_ = std::vector<std::string>(targrows);
    return Status::OK();
  }

  Result<bool> ProcessNextBlock() {
    // Process previous responses
    for (auto rowset_iter = rowsets_.begin(); rowset_iter != rowsets_.end();) {
      if (rowset_iter->is_eof()) {
        rowset_iter = rowsets_.erase(rowset_iter);
      } else {
        // Update reservoir with newly selected rows.
        RETURN_NOT_OK(rowset_iter->ProcessSparseSystemColumns(reservoir_.data()));
        return true;
      }
    }
    // Request more data, if exhausted, mark reservoir as ready and let caller know
    if (VERIFY_RESULT(FetchDataFromServer())) {
      return true;
    } else {
      // Skip fetch if the table is empty
      reservoir_ready_ = !reservoir_.front().empty();
      return false;
    }
  }

  Result<const std::vector<Slice>*> FetchYbctidBatch() override {
    // Check if all ybctids are already returned
    if (!reservoir_ready_) {
      return nullptr;
    }
    // Get reservoir capacity. There may be less rows then that
    int targrows = read_req_->sampling_state().targrows();
    // Prepare target vector
    ybctids_.clear();
    ybctids_.reserve(targrows);
    // Create pointers to the items in the reservoir
    for (const auto& ybctid : reservoir_) {
      if (ybctid.empty()) {
        // Algorithm fills up the reservoir first. Empty row means there are no more data
        break;
      }
      ybctids_.emplace_back(ybctid);
    }
    reservoir_ready_ = false;
    return &ybctids_;
  }

  Result<EstimatedRowCount> GetEstimatedRowCount() const {
    return down_cast<const PgDocReadOp*>(doc_op_.get())->GetEstimatedRowCount();
  }

 private:
  // The reservoir to keep ybctids of selected sample rows
  std::vector<std::string> reservoir_;
  // If true sampling is completed and ybctids can be collected from the reservoir
  bool reservoir_ready_ = false;
  // Vector of Slices pointing to the values in the reservoir
  std::vector<Slice> ybctids_;
};

PgSample::PgSample(
    PgSession::ScopedRefPtr pg_session,
    int targrows, const PgObjectId& table_id, bool is_region_local)
    : PgDmlRead(pg_session, table_id, is_region_local), targrows_(targrows) {}

Status PgSample::Prepare() {
  // Setup target and bind descriptor.
  target_ = PgTable(VERIFY_RESULT(LoadTable()));
  bind_ = PgTable(nullptr);

  // Setup sample picker as secondary index query
  secondary_index_query_ = std::make_unique<PgSamplePicker>(
      pg_session_, table_id_, is_region_local_);
  RETURN_NOT_OK(secondary_index_query_->Prepare());

  // Prepare read op to fetch rows
  auto read_op = ArenaMakeShared<PgsqlReadOp>(
      arena_ptr(), &arena(), *target_, is_region_local_,
      pg_session_->metrics().metrics_capture());
  read_req_ = std::shared_ptr<LWPgsqlReadRequestPB>(read_op, &read_op->read_request());
  doc_op_ = make_shared<PgDocReadOp>(pg_session_, &target_, std::move(read_op));

  return Status::OK();
}

Status PgSample::InitRandomState(double rstate_w, uint64_t rand_state_s0, uint64_t rand_state_s1) {
  return VERIFY_RESULT_REF(SamplePicker()).PrepareSamplingState(
      targrows_, rstate_w, rand_state_s0, rand_state_s1);
}

Result<bool> PgSample::SampleNextBlock() {
  auto& picker = VERIFY_RESULT_REF(SamplePicker());
  RETURN_NOT_OK(ExecSecondaryIndexOnce());
  return picker.ProcessNextBlock();
}

Result<EstimatedRowCount> PgSample::GetEstimatedRowCount() {
  return VERIFY_RESULT_REF(SamplePicker()).GetEstimatedRowCount();
}

Result<PgSamplePicker&> PgSample::SamplePicker() {
  RSTATUS_DCHECK(secondary_index_query_ != nullptr, RuntimeError, "PgSample has not been prepared");
  return *down_cast<PgSamplePicker*>(secondary_index_query_.get());
}

} // namespace yb::pggate
