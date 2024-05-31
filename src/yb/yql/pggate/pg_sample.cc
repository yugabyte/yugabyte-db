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

#include "yb/gutil/casts.h"

namespace yb {
namespace pggate {

using std::make_shared;
using std::vector;

//--------------------------------------------------------------------------------------------------
// PgSample
//--------------------------------------------------------------------------------------------------

PgSample::PgSample(PgSession::ScopedRefPtr pg_session,
                   int targrows,
                   const PgObjectId& table_id,
                   bool is_region_local)
    : PgDmlRead(pg_session, table_id, PgObjectId(), nullptr, is_region_local),
      targrows_(targrows) {}

PgSample::~PgSample() {
}

Status PgSample::Prepare() {
  // Setup target and bind descriptor.
  target_ = PgTable(VERIFY_RESULT(pg_session_->LoadTable(table_id_)));
  bind_ = PgTable(nullptr);

  // Setup sample picker as secondary index query
  secondary_index_query_ = std::make_unique<PgSamplePicker>(
      pg_session_, table_id_, is_region_local_);
  RETURN_NOT_OK(secondary_index_query_->Prepare());

  // Prepare read op to fetch rows
  auto read_op = ArenaMakeShared<PgsqlReadOp>(
      arena_ptr(), &arena(), *target_, is_region_local_, pg_session_->metrics().metrics_capture());
  read_req_ = std::shared_ptr<LWPgsqlReadRequestPB>(read_op, &read_op->read_request());
  doc_op_ = make_shared<PgDocReadOp>(pg_session_, &target_, std::move(read_op));

  return Status::OK();
}

Status PgSample::InitRandomState(double rstate_w, uint64_t rand_state_s0, uint64_t rand_state_s1) {
  SCHECK(secondary_index_query_ != nullptr, RuntimeError, "PgSample has not been prepared");
  return down_cast<PgSamplePicker*>(secondary_index_query_.get())
      ->PrepareSamplingState(targrows_, rstate_w, rand_state_s0, rand_state_s1);
}

Status PgSample::SampleNextBlock(bool *has_more) {
  SCHECK(secondary_index_query_ != nullptr, RuntimeError, "PgSample has not been prepared");
  if (!secondary_index_query_->is_executed()) {
    secondary_index_query_->set_is_executed(true);
    RETURN_NOT_OK(secondary_index_query_->Exec(nullptr));
  }

  auto picker = down_cast<PgSamplePicker*>(secondary_index_query_.get());
  *has_more = VERIFY_RESULT(picker->ProcessNextBlock());

  return Status::OK();
}

Status PgSample::GetEstimatedRowCount(double *liverows, double *deadrows) {
  SCHECK(secondary_index_query_ != nullptr, RuntimeError, "PgSample has not been prepared");
  return down_cast<PgSamplePicker*>(secondary_index_query_.get())
      ->GetEstimatedRowCount(liverows, deadrows);
}

//--------------------------------------------------------------------------------------------------
// PgSamplePicker
//--------------------------------------------------------------------------------------------------

PgSamplePicker::PgSamplePicker(PgSession::ScopedRefPtr pg_session,
                               const PgObjectId& table_id,
                               bool is_region_local)
    : PgSelectIndex(pg_session, table_id, PgObjectId(), nullptr, is_region_local) {}

PgSamplePicker::~PgSamplePicker() {
}

Status PgSamplePicker::Prepare() {
  target_ = PgTable(VERIFY_RESULT(pg_session_->LoadTable(table_id_)));
  bind_ = PgTable(nullptr);
  auto read_op = ArenaMakeShared<PgsqlReadOp>(
      arena_ptr(), &arena(), *target_, is_region_local_, pg_session_->metrics().metrics_capture());
  read_req_ = std::shared_ptr<LWPgsqlReadRequestPB>(read_op, &read_op->read_request());
  doc_op_ = make_shared<PgDocReadOp>(pg_session_, &target_, std::move(read_op));
  return Status::OK();
}

Status PgSamplePicker::PrepareSamplingState(
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
  reservoir_ = std::make_unique<std::string[]>(targrows);
  return Status::OK();
}

Result<bool> PgSamplePicker::ProcessNextBlock() {
  // Process previous responses
  for (auto rowset_iter = rowsets_.begin(); rowset_iter != rowsets_.end();) {
    if (rowset_iter->is_eof()) {
      rowset_iter = rowsets_.erase(rowset_iter);
    } else {
      // Update reservoir with newly selected rows.
      RETURN_NOT_OK(rowset_iter->ProcessSparseSystemColumns(reservoir_.get()));
      return true;
    }
  }
  // Request more data, if exhausted, mark reservoir as ready and let caller know
  if (VERIFY_RESULT(FetchDataFromServer())) {
    return true;
  } else {
    // Skip fetch if the table is empty
    reservoir_ready_ = !reservoir_[0].empty();
    return false;
  }
}

Result<bool> PgSamplePicker::FetchYbctidBatch(const vector<Slice> **ybctids) {
  // Check if all ybctids are already returned
  if (!reservoir_ready_) {
    *ybctids = nullptr;
    return false;
  }
  // Get reservoir capacity. There may be less rows then that
  int targrows = read_req_->sampling_state().targrows();
  // Prepare target vector
  ybctids_.clear();
  ybctids_.reserve(targrows);
  // Create pointers to the items in the reservoir
  for (int idx = 0; idx < targrows; idx++) {
    if (reservoir_[idx].empty()) {
      // Algorithm fills up the reservoir first. Empty row means there are
      // no mmore data
      break;
    }
    ybctids_.emplace_back(reservoir_[idx]);
  }
  reservoir_ready_ = false;
  *ybctids = &ybctids_;
  return true;
}

Status PgSamplePicker::GetEstimatedRowCount(double *liverows, double *deadrows) {
  return down_cast<PgDocReadOp*>(doc_op_.get())->GetEstimatedRowCount(liverows, deadrows);
}

}  // namespace pggate
}  // namespace yb
