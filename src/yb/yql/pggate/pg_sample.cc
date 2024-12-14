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

#include <string>
#include <vector>
#include <utility>

#include "yb/common/read_hybrid_time.h"

#include "yb/gutil/casts.h"

#include "yb/util/atomic.h"
#include "yb/util/logging.h"

#include "yb/yql/pggate/pg_select_index.h"

DEFINE_test_flag(int64, delay_after_table_analyze_ms, 0,
    "Add this delay after each table is analyzed.");

namespace yb::pggate {

// Internal class to work as the secondary index to select sample tuples.
// Like index, it produces ybctids of random records and outer PgSample fetches them.
// Unlike index, it does not use a secondary index, but scans main table instead.
class PgSamplePicker : public PgSelectIndex {
 public:
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

  EstimatedRowCount GetEstimatedRowCount() const {
    AtomicFlagSleepMs(&FLAGS_TEST_delay_after_table_analyze_ms);
    return down_cast<const PgDocReadOp*>(doc_op_.get())->GetEstimatedRowCount();
  }

  static Result<std::unique_ptr<PgSamplePicker>> Make(
      const PgSession::ScopedRefPtr& pg_session, const PgObjectId& table_id, bool is_region_local,
      int targrows, const SampleRandomState& rand_state, HybridTime read_time) {
    std::unique_ptr<PgSamplePicker> result{new PgSamplePicker{pg_session}};
    RETURN_NOT_OK(result->Prepare(table_id, is_region_local, targrows, rand_state, read_time));
    return result;
  }

 private:
  explicit PgSamplePicker(const PgSession::ScopedRefPtr& pg_session)
      : PgSelectIndex(pg_session) {
  }

  Status Prepare(
      const PgObjectId& table_id, bool is_region_local, int targrows,
      const SampleRandomState& rand_state, HybridTime read_time) {
    target_ = PgTable(VERIFY_RESULT(pg_session_->LoadTable(table_id)));
    bind_ = PgTable(nullptr);
    auto read_op = ArenaMakeShared<PgsqlReadOp>(
        arena_ptr(), &arena(), *target_, is_region_local, pg_session_->metrics().metrics_capture());
    // Use the same time as PgSample. Otherwise, ybctids may be gone
    // when PgSample tries to fetch the rows.
    read_op->set_read_time(ReadHybridTime::SingleTime(read_time));
    read_req_ = std::shared_ptr<LWPgsqlReadRequestPB>(read_op, &read_op->read_request());
    doc_op_ = std::make_shared<PgDocReadOp>(pg_session_, &target_, std::move(read_op));

    reservoir_.insert(reservoir_.begin(), targrows, {});
    ybctids_.reserve(targrows);
    auto& sampling_state = *read_req_->mutable_sampling_state();
    sampling_state.set_targrows(targrows);      // target sample size
    sampling_state.set_numrows(0);              // current number of rows selected
    sampling_state.set_samplerows(0);           // rows scanned so far
    sampling_state.set_rowstoskip(-1);          // rows to skip before selecting another
    sampling_state.set_rstate_w(rand_state.w);  // Vitter algorithm's W
    auto& rand = *sampling_state.mutable_rand_state();
    rand.set_s0(rand_state.s0);
    rand.set_s1(rand_state.s1);
    return Status::OK();
  }

  Result<const std::vector<Slice>*> DoFetchYbctidBatch() override {
    // Check if all ybctids are already returned
    if (!reservoir_ready_) {
      return nullptr;
    }
    // Prepare target vector
    ybctids_.clear();
    // Create pointers to the items in the reservoir
    for (const auto& ybctid : reservoir_) {
      if (ybctid.empty()) {
        // Algorithm fills up the reservoir first. Empty row means there are no more data
        break;
      }
      ybctids_.push_back(ybctid);
    }
    reservoir_ready_ = false;
    return &ybctids_;
  }

  // The reservoir to keep ybctids of selected sample rows
  std::vector<std::string> reservoir_;
  // If true sampling is completed and ybctids can be collected from the reservoir
  bool reservoir_ready_ = false;
  // Vector of Slices pointing to the values in the reservoir
  std::vector<Slice> ybctids_;
};

PgSample::PgSample(const PgSession::ScopedRefPtr& pg_session)
    : BaseType(pg_session) {}

Status PgSample::Prepare(
    const PgObjectId& table_id, bool is_region_local, int targrows,
    const SampleRandomState& rand_state, HybridTime read_time) {
  // Setup target and bind descriptor.
  target_ = PgTable(VERIFY_RESULT(pg_session_->LoadTable(table_id)));
  bind_ = PgTable(nullptr);

  SetSecondaryIndex(VERIFY_RESULT(PgSamplePicker::Make(
      pg_session_, table_id, is_region_local, targrows, rand_state, read_time)));

  // Prepare read op to fetch rows
  auto read_op = ArenaMakeShared<PgsqlReadOp>(
      arena_ptr(), &arena(), *target_, is_region_local,
      pg_session_->metrics().metrics_capture());
  // Clamp the read uncertainty window to avoid read restart errors.
  read_op->set_read_time(ReadHybridTime::SingleTime(read_time));
  read_req_ = std::shared_ptr<LWPgsqlReadRequestPB>(read_op, &read_op->read_request());
  doc_op_ = make_shared<PgDocReadOp>(pg_session_, &target_, std::move(read_op));

  VLOG_WITH_FUNC(3)
    << "Sampling table: " << target_->table_name().table_name()
    << " for " << targrows << " rows"
    << " using read time: " << read_time;

  return Status::OK();
}

Result<bool> PgSample::SampleNextBlock() {
  RETURN_NOT_OK(DCHECK_NOTNULL(SecondaryIndex())->Execute());
  return SamplePicker().ProcessNextBlock();
}

EstimatedRowCount PgSample::GetEstimatedRowCount() {
  return SamplePicker().GetEstimatedRowCount();
}

PgSamplePicker& PgSample::SamplePicker() {
  return *down_cast<PgSamplePicker*>(DCHECK_NOTNULL(SecondaryIndexQuery()));
}

Result<std::unique_ptr<PgSample>> PgSample::Make(
    const PgSession::ScopedRefPtr& pg_session, const PgObjectId& table_id, bool is_region_local,
    int targrows, const SampleRandomState& rand_state, HybridTime read_time) {
  std::unique_ptr<PgSample> result{new PgSample{pg_session}};
  RETURN_NOT_OK(result->Prepare(table_id, is_region_local, targrows, rand_state, read_time));
  return result;
}

} // namespace yb::pggate
