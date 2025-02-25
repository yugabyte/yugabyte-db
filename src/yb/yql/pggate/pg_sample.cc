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

#include "yb/common/common.pb.h"
#include "yb/common/read_hybrid_time.h"

#include "yb/gutil/casts.h"

#include "yb/util/atomic.h"
#include "yb/util/logging.h"

#include "yb/util/flags/flag_tags.h"

#include "yb/yql/pggate/pg_select_index.h"

#include "yb/yql/pggate/util/ybc_guc.h"

DEFINE_test_flag(int64, delay_after_table_analyze_ms, 0,
    "Add this delay after each table is analyzed.");

DEFINE_RUNTIME_int32(
    ysql_docdb_blocks_sampling_method, yb::DocDbBlocksSamplingMethod::SPLIT_INTERSECTING_BLOCKS_V3,
    "Controls how we define blocks for 1st phase of block-based sampling.");
TAG_FLAG(ysql_docdb_blocks_sampling_method, hidden);

namespace yb::pggate {

namespace {

class PgDocSampleOp : public PgDocReadOp {
 public:
  PgDocSampleOp(const PgSession::ScopedRefPtr& pg_session, PgTable* table, PgsqlReadOpPtr read_op)
      : PgDocReadOp(pg_session, table, std::move(read_op)) {}

  // Create one sampling operator per partition and arrange their execution in random order
  Result<bool> DoCreateRequests() override {
    // Create one PgsqlOp per partition
    ClonePgsqlOps(table_->GetPartitionListSize());
    // Partitions are sampled sequentially, one at a time
    parallelism_level_ = 1;
    // Assign partitions to operators.
    const auto& partition_keys = table_->GetPartitionList();
    SCHECK_EQ(
        partition_keys.size(), pgsql_ops_.size(), IllegalState,
        "Number of partitions and number of partition keys are not the same");

    // Bind requests to partitions
    for (size_t partition = 0; partition < partition_keys.size(); ++partition) {
      // Use partition index to setup the protobuf to identify the partition that this request
      // is for. Batcher will use this information to send the request to correct tablet server, and
      // server uses this information to operate on correct tablet.
      // - Range partition uses range partition key to identify partition.
      // - Hash partition uses "next_partition_key" and "max_hash_code" to identify partition.
      if (VERIFY_RESULT(SetLowerUpperBound(&GetReadReq(partition), partition))) {
        // Currently we do not set boundaries on sampling requests other than partition boundaries,
        // so result is going to be always true, though that may change.
        pgsql_ops_[partition]->set_active(true);
        ++active_op_count_;
      }
    }
    // Got some inactive operations, move them away
    if (active_op_count_ < pgsql_ops_.size()) {
      MoveInactiveOpsOutside();
    }
    VLOG(1) << "Number of partitions to sample: " << active_op_count_;

    return true;
  }

  Status CompleteProcessResponse() override {
    const auto send_count = std::min(parallelism_level_, active_op_count_);

    // There can be at most one op at a time for sampling, since any modifications to the random
    // sampling state need to be propagated after one op completes to the next.
    SCHECK_LE(
        send_count, size_t{1}, IllegalState,
        "We should send at most 1 sampling request at a time.");
    if (send_count == 0) {
      // Let super class to complete processing if still needed.
      return PgDocReadOp::CompleteProcessResponse();
    }

    auto& res = *GetReadOp(0).response();
    SCHECK(res.has_sampling_state(), IllegalState, "Sampling response should have sampling state");
    auto* sampling_state = res.mutable_sampling_state();
    VLOG_WITH_FUNC(1) << "Received sampling state: " << sampling_state->ShortDebugString();
    estimated_total_rows_ = sampling_state->has_estimated_total_rows()
                                ? sampling_state->estimated_total_rows()
                                : sampling_state->samplerows();

    RETURN_NOT_OK(PgDocReadOp::CompleteProcessResponse());

    if (active_op_count_ > 0) {
      auto& next_active_op = GetReadOp(0);
      next_active_op.read_request().ref_sampling_state(sampling_state);
      VLOG_WITH_FUNC(1) << "Continue sampling from " << sampling_state->ShortDebugString()
                        << " for " << &next_active_op;
    }

    return Status::OK();
  }

  EstimatedRowCount GetEstimatedRowCount() const {
    VLOG(1) << "Returning liverows " << estimated_total_rows_;
    // Postgres wants estimation of dead tuples count to trigger vacuuming, but it is unlikely it
    // will be useful for us.
    return EstimatedRowCount{.live = estimated_total_rows_, .dead = 0};
  }

 private:
  double estimated_total_rows_ = 0;
};

} // namespace

// Internal class to select sample rows ybctids.
class PgSample::SamplePicker : public PgSelect {
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
    return GetSampleOp().GetEstimatedRowCount();
  }

  static Result<std::unique_ptr<SamplePicker>> Make(
      const PgSession::ScopedRefPtr& pg_session, const PgObjectId& table_id, bool is_region_local,
      int targrows, const SampleRandomState& rand_state, HybridTime read_time) {
    std::unique_ptr<SamplePicker> result{new SamplePicker{pg_session}};
    RETURN_NOT_OK(result->Prepare(table_id, is_region_local, targrows, rand_state, read_time));
    return result;
  }

  const std::vector<Slice>& FetchYbctids() {
    ybctids_.clear();
    if (!reservoir_ready_) {
      return ybctids_;
    }
    for (const auto& ybctid : reservoir_) {
      if (ybctid.empty()) {
        // Algorithm fills up the reservoir first. Empty row means there are no more data
        break;
      }
      ybctids_.push_back(ybctid);
    }
    reservoir_ready_ = false;
    return ybctids_;
  }

 private:
  explicit SamplePicker(const PgSession::ScopedRefPtr& pg_session)
      : PgSelect(pg_session) {
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
    doc_op_ = std::make_shared<PgDocSampleOp>(pg_session_, &target_, std::move(read_op));

    reservoir_.insert(reservoir_.begin(), targrows, {});
    ybctids_.reserve(targrows);
    auto& sampling_state = *read_req_->mutable_sampling_state();
    sampling_state.set_targrows(targrows);      // target sample size
    sampling_state.set_numrows(0);              // current number of rows selected
    sampling_state.set_samplerows(0);           // rows scanned so far
    sampling_state.set_rowstoskip(-1);          // rows to skip before selecting another
    sampling_state.set_rstate_w(rand_state.w);  // Vitter algorithm's W
    if (yb_allow_block_based_sampling_algorithm) {
      sampling_state.set_sampling_algorithm(YsqlSamplingAlgorithm(yb_sampling_algorithm));
    } else {
      sampling_state.set_sampling_algorithm(YsqlSamplingAlgorithm::FULL_TABLE_SCAN);
    }
    sampling_state.set_docdb_blocks_sampling_method(
        DocDbBlocksSamplingMethod(FLAGS_ysql_docdb_blocks_sampling_method));
    auto& rand = *sampling_state.mutable_rand_state();
    rand.set_s0(rand_state.s0);
    rand.set_s1(rand_state.s1);
    return Status::OK();
  }

  PgDocSampleOp& GetSampleOp() const { return down_cast<PgDocSampleOp&>(*doc_op_); }

  // The reservoir to keep ybctids of selected sample rows
  std::vector<std::string> reservoir_;
  // If true sampling is completed and ybctids can be collected from the reservoir
  bool reservoir_ready_ = false;
  // Vector of Slices pointing to the values in the reservoir
  std::vector<Slice> ybctids_;
};

PgSample::PgSample(const PgSession::ScopedRefPtr& pg_session)
    : BaseType(pg_session) {}

PgSample::~PgSample() {}

Status PgSample::Prepare(
    const PgObjectId& table_id, bool is_region_local, int targrows,
    const SampleRandomState& rand_state, HybridTime read_time) {
  // Setup target and bind descriptor.
  target_ = PgTable(VERIFY_RESULT(pg_session_->LoadTable(table_id)));
  bind_ = PgTable(nullptr);

  sample_picker_ = VERIFY_RESULT(SamplePicker::Make(
      pg_session_, table_id, is_region_local, targrows, rand_state, read_time));

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

  RETURN_NOT_OK(sample_picker_->Exec(pg_exec_params_));

  return Status::OK();
}

Result<bool> PgSample::SampleNextBlock() {
  const auto continue_sampling = VERIFY_RESULT(sample_picker_->ProcessNextBlock());
  if (!continue_sampling) {
    const auto& ybctids = sample_picker_->FetchYbctids();
    if (!ybctids.empty()) {
      SetRequestedYbctids(ybctids);
    }
  }
  return continue_sampling;
}

EstimatedRowCount PgSample::GetEstimatedRowCount() {
  return sample_picker_->GetEstimatedRowCount();
}

Result<std::unique_ptr<PgSample>> PgSample::Make(
    const PgSession::ScopedRefPtr& pg_session, const PgObjectId& table_id, bool is_region_local,
    int targrows, const SampleRandomState& rand_state, HybridTime read_time) {
  std::unique_ptr<PgSample> result{new PgSample{pg_session}};
  RETURN_NOT_OK(result->Prepare(table_id, is_region_local, targrows, rand_state, read_time));
  return result;
}

} // namespace yb::pggate
