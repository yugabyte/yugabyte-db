//--------------------------------------------------------------------------------------------------
// Copyright (c) YugabyteDB, Inc.
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

DEFINE_test_flag(bool, refresh_partitions_after_fetched_sample_blocks, false,
    "Force table partitions refresh after sample blocks are fetched.");

DEFINE_RUNTIME_int32(
    ysql_docdb_blocks_sampling_method, yb::DocDbBlocksSamplingMethod::SPLIT_INTERSECTING_BLOCKS_V3,
    "Controls how we define blocks for 1st phase of block-based sampling.");
TAG_FLAG(ysql_docdb_blocks_sampling_method, hidden);

DECLARE_uint64(rpc_max_message_size);
DECLARE_double(max_buffer_size_to_rpc_limit_ratio);

namespace yb::pggate {

// Internal interface to select sample rows ybctids.
class SampleRowsPickerIf {
 public:
  virtual ~SampleRowsPickerIf() = default;

  virtual Status Exec() = 0;
  virtual Result<bool> ProcessNextBlock() = 0;
  virtual Result<const std::vector<Slice>&> FetchYbctids() = 0;
  virtual double GetEstimatedRowCount() const = 0;
};

namespace {

std::string AsDebugHexString(const LWPgsqlSampleBlockPB& sample_block_pb) {
  return AsDebugHexString(
      std::make_pair(sample_block_pb.lower_bound(), sample_block_pb.upper_bound()));
}

class PgDocSampleOp : public PgDocReadOp {
 public:
  struct SamplingStats {
    uint64_t num_blocks_processed;
    uint64_t num_blocks_collected;
    double num_rows_processed;
    int32 num_rows_collected;
  };

  PgDocSampleOp(const PgSession::ScopedRefPtr& pg_session, PgTable* table, PgsqlReadOpPtr read_op)
      : PgDocReadOp(pg_session, table, std::move(read_op)),
        log_prefix_(Format("PgDocSampleOp($0): ", static_cast<void*>(this))) {}

  // Create one sampling operator per partition and arrange their execution in random order
  Result<bool> DoCreateRequests() override {
    VLOG_WITH_PREFIX_AND_FUNC(1) << "Preparing sampling requests";

    const auto& template_read_req = GetTemplateReadReq();
    SCHECK(
        template_read_req.has_sampling_state(), IllegalState,
        "PgDocSampleOp is expected to have sampling state");
    SCHECK(
        sorted_sample_blocks_.empty() ||
            !template_read_req.sampling_state().is_blocks_sampling_stage(),
        IllegalState, "Sample blocks are not expected to be set for blocks sampling stage");

    // Sample blocks will be distributed across tablets/table partitions below.
    std::optional<SampleBlocksFeed> sample_blocks_feed;
    if (!sorted_sample_blocks_.empty()) {
      sample_blocks_feed.emplace(sorted_sample_blocks_);
    }

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
      auto& read_op = GetReadOp(partition);
      auto& read_req = read_op.read_request();
      if (VERIFY_RESULT(SetLowerUpperBound(&read_req, partition))) {
        // Currently we do not set boundaries on sampling requests other than partition boundaries,
        // so result is going to be always true, though that may change.
        if (!sample_blocks_feed.has_value() ||
            VERIFY_RESULT(AssignSampleBlocks(
                &read_req, partition_keys, partition, &sample_blocks_feed.value()))) {
          read_op.set_active(true);
          VLOG_WITH_PREFIX_AND_FUNC(3)
              << "Request #" << partition << " of " << partition_keys.size()
              << " for partition: " << Slice(partition_keys[partition]).ToDebugHexString();
        }
      }
    }

    const auto active_op_count = MoveInactiveOpsOutside();

    VLOG_WITH_PREFIX_AND_FUNC(1) << "Number of partitions to sample: " << active_op_count;

    return true;
  }

  Status CompleteProcessResponse() override {
    if (!HasActiveOps()) {
      // Let super class to complete processing if still needed.
      return PgDocReadOp::CompleteProcessResponse();
    }

    // There can be one op at a time for sampling, since any modifications to the random
    // sampling state need to be propagated after one op completes to the next.
    RSTATUS_DCHECK(
        parallelism_level_ < 2 || pgsql_ops_.size() < 2 || !pgsql_ops_[1]->is_active(),
        IllegalState, "We should send single sampling request at a time.");

    auto& res = *pgsql_ops_.front()->response();
    SCHECK(res.has_sampling_state(), IllegalState, "Sampling response should have sampling state");
    auto* sampling_state = res.mutable_sampling_state();
    VLOG_WITH_PREFIX_AND_FUNC(1) << "Received sampling state: "
                                 << sampling_state->ShortDebugString();
    SCHECK(sampling_state->has_rand_state(), InvalidArgument,
           "Invalid sampling state, random state is missing");
    sampling_stats_ = {
      .num_blocks_processed = sampling_state->num_blocks_processed(),
      .num_blocks_collected = sampling_state->num_blocks_collected(),
      .num_rows_processed = sampling_state->samplerows(),
      .num_rows_collected = sampling_state->numrows(),
    };

    RETURN_NOT_OK(PgDocReadOp::CompleteProcessResponse());

    if (HasActiveOps()) {
      auto& next_active_op = GetReadOp(0);
      next_active_op.read_request().ref_sampling_state(sampling_state);
      VLOG_WITH_PREFIX_AND_FUNC(1)
          << "Continue sampling from " << sampling_state->ShortDebugString() << " for "
          << &next_active_op;
    }

    return Status::OK();
  }

  const SamplingStats& GetSamplingStats() const { return sampling_stats_; }

  Status SetSampleBlocksBounds(std::vector<std::pair<KeyBuffer, KeyBuffer>>&& sample_blocks) {
    sorted_sample_blocks_ = std::move(sample_blocks);
    SCHECK(!sorted_sample_blocks_.empty(), IllegalState, "Sample blocks list should not be empty.");

    std::sort(
        sorted_sample_blocks_.begin(), sorted_sample_blocks_.end(),
        [](const std::pair<KeyBuffer, KeyBuffer>& b1, const std::pair<KeyBuffer, KeyBuffer>& b2) {
          return b1.first < b2.first;
        });

    if (VLOG_IS_ON(3)) {
      size_t idx = 0;
      for (const auto& sample_block : sorted_sample_blocks_) {
        VLOG_WITH_FUNC(3) << "Sorted sample block #" << idx << ": "
                          << AsDebugHexString(sample_block);
        ++idx;
      }
    }

    Slice prev_upper_bound;
    size_t idx = 0;
    for (const auto& sample_block : sorted_sample_blocks_) {
      if (sample_block.first.AsSlice() < prev_upper_bound) {
        return STATUS_FORMAT(
            InternalError, "Sorted sample block #$0: $1 starts before prev_upper_bound: $2", idx,
            AsDebugHexString(sample_block), AsDebugHexString(prev_upper_bound));
      }
      prev_upper_bound = sample_block.second.AsSlice();
      ++idx;
    }

    if (FLAGS_TEST_refresh_partitions_after_fetched_sample_blocks) {
      const auto pg_table_id = table_->pg_table_id();
      pg_session_->InvalidateTableCache(pg_table_id, InvalidateOnPgClient::kTrue);
      table_ = PgTable(CHECK_RESULT(pg_session_->LoadTable(pg_table_id)));
    }
    return Status::OK();
  }

 private:
  // Internal class for assigning sample block boundaries across per-tablet sampling read requests.
  class SampleBlocksFeed {
   public:
    // Transfers all sample blocks from `other` list into internal storage.
    explicit SampleBlocksFeed(
        const std::vector<std::pair<KeyBuffer, KeyBuffer>>& sorted_sample_blocks)
        : sorted_sample_blocks_(sorted_sample_blocks) {
      sample_block_iter_ = sorted_sample_blocks_.cbegin();
      is_single_unbounded_block_ = sample_block_iter_ != sorted_sample_blocks_.cend() &&
                                   sample_block_iter_->first.empty() &&
                                   sample_block_iter_->second.empty();
    }

    // Fetches sample block boundaries from internal storage until `exclusive_upper_bound` and
    // assigns them to `dst`.
    Status FetchTo(
        ::yb::ArenaList<LWPgsqlSampleBlockPB>* dst, const std::string& exclusive_upper_bound) {
      if (is_single_unbounded_block_) {
        // We should fully sample all tablets.
        auto& sample_block_pb = *dst->Add();
        *sample_block_pb.mutable_lower_bound() = Slice();
        *sample_block_pb.mutable_upper_bound() = Slice();
        return Status::OK();
      }

      for (; sample_block_iter_ != sorted_sample_blocks_.cend() &&
             (exclusive_upper_bound.empty() ||
              (!sample_block_iter_->second.empty() &&
               sample_block_iter_->second.AsSlice() <= exclusive_upper_bound));
           sample_block_iter_++) {
        LWPgsqlSampleBlockPB* sample_block_pb;

        if (!override_next_block_lower_bound_.empty()) {
          SCHECK(
              dst->empty(), InternalError,
              Format(
                  "Expected dst (has $0 blocks) to be empty when override_next_block_lower_bound_ "
                  "is set "
                  "($1)",
                  dst->size(), AsDebugHexString(override_next_block_lower_bound_)));
          sample_block_pb = dst->Add();
          sample_block_pb->dup_lower_bound(override_next_block_lower_bound_.AsSlice());
          override_next_block_lower_bound_.clear();
        } else {
          sample_block_pb = AddOrUpdateBlock(dst, sample_block_iter_->first.AsSlice());
        }

        *sample_block_pb->mutable_upper_bound() = sample_block_iter_->second.AsSlice();
        RETURN_NOT_OK(OnLatestBlockBoundsSet(dst));
      }

      SCHECK(
          !exclusive_upper_bound.empty() || sample_block_iter_ == sorted_sample_blocks_.cend(),
          InternalError,
          Format(
              "Unexpected stop at sorted sample block $0 while exclusive_upper_bound is empty",
              AsDebugHexString(*sample_block_iter_)));

      if (sample_block_iter_ == sorted_sample_blocks_.cend() ||
          sample_block_iter_->first.AsSlice() >= exclusive_upper_bound) {
        return Status::OK();
      }

      // Sample block might cross exclusive_upper_bound due to tablet has been split since
      // block boundaries were calculated - split the block in this case.
      VLOG_WITH_FUNC(1)
          << "Splitting the sample block: " << AsDebugHexString(*sample_block_iter_)
          << " exclusive_upper_bound: " << AsDebugHexString(Slice(exclusive_upper_bound));

      auto* sample_block_pb = AddOrUpdateBlock(dst, sample_block_iter_->first.AsSlice());
      sample_block_pb->dup_upper_bound(exclusive_upper_bound);
      RETURN_NOT_OK(OnLatestBlockBoundsSet(dst));

      override_next_block_lower_bound_ = exclusive_upper_bound;

      return Status::OK();
    }

   private:
    LWPgsqlSampleBlockPB* AddOrUpdateBlock(
        ::yb::ArenaList<LWPgsqlSampleBlockPB>* dst, Slice lower_bound) {
      if (!dst->empty() && dst->back().upper_bound() == sample_block_iter_->first.AsSlice()) {
        // Update previous block to combine with the current one.
        return &dst->back();
      }
      auto* block = dst->Add();
      *block->mutable_lower_bound() = lower_bound;
      return block;
    }

    Status OnLatestBlockBoundsSet(::yb::ArenaList<LWPgsqlSampleBlockPB>* dst) {
      auto* sample_block_pb = &dst->back();
      VLOG_WITH_FUNC(2) << "Sample block at dst[" << dst->size() - 1 << "]: "
                        << AsDebugHexString(*sample_block_pb);
      SCHECK(
          sample_block_pb->upper_bound().empty() ||
              sample_block_pb->lower_bound() < sample_block_pb->upper_bound(),
          InternalError,
          Format(
              "Wrong bounds order for sample block: $0",
              AsDebugHexString(*sample_block_pb)));
      return Status::OK();
    }

    const std::vector<std::pair<KeyBuffer, KeyBuffer>>& sorted_sample_blocks_;
    std::vector<std::pair<KeyBuffer, KeyBuffer>>::const_iterator sample_block_iter_;
    bool is_single_unbounded_block_;
    // Not empty iff we've split sample block at sample_block_iter_ during previous FetchTo call.
    // In this case we use the rest of this block starting with override_next_block_lower_bound_
    // for the next FetchTo call.
    KeyBuffer override_next_block_lower_bound_;
  };

  Result<bool> AssignSampleBlocks(
      LWPgsqlReadRequestPB* request, const client::TablePartitionList& partition_keys,
      size_t partition, SampleBlocksFeed* sample_blocks_feed) {
    std::string next_encoded_partition_key;
    if (partition + 1 < partition_keys.size()) {
      next_encoded_partition_key = VERIFY_RESULT(
          table_->partition_schema().GetEncodedPartitionKey(partition_keys[partition + 1]));
    }
    auto* req_sample_blocks = request->mutable_sample_blocks();
    RETURN_NOT_OK(sample_blocks_feed->FetchTo(req_sample_blocks, next_encoded_partition_key));
    const auto num_blocks_assigned = req_sample_blocks->size();

    VLOG_WITH_PREFIX_AND_FUNC(1)
        << "Number of sample blocks assigned to partition "
        << AsDebugHexString(std::make_pair(
               Slice(VERIFY_RESULT(
                   table_->partition_schema().GetEncodedPartitionKey(partition_keys[partition]))),
               Slice(next_encoded_partition_key)))
        << ": " << num_blocks_assigned;

    return num_blocks_assigned > 0;
  }

  const std::string LogPrefix() const { return log_prefix_; }

  std::vector<std::pair<KeyBuffer, KeyBuffer>> sorted_sample_blocks_;
  SamplingStats sampling_stats_;
  std::string log_prefix_;
};

class SamplePickerBase : public PgSelect {
 public:
  virtual Status ProcessResultEntry(Slice* data) = 0;

  virtual Status FetchDone() = 0;

  Result<bool> ProcessNextBlock() {
    if (!VERIFY_RESULT(doc_op_->ResultStream().ProcessNextSysEntries(
            [this](Slice& data) {
              return ProcessResultEntry(&data);
            }))) {
      RETURN_NOT_OK(FetchDone());
      return false;
    }

    return true;
  }

 protected:
  explicit SamplePickerBase(const PgSession::ScopedRefPtr& pg_session) : PgSelect(pg_session) {
  }

  PgDocSampleOp& GetSampleOp() const { return down_cast<PgDocSampleOp&>(*doc_op_); }

  Status Prepare(
      const PgObjectId& table_id, const YbcPgTableLocalityInfo& locality_info,
      HybridTime read_time) {
    target_ = PgTable(VERIFY_RESULT(pg_session_->LoadTable(table_id)));
    bind_ = PgTable(nullptr);
    auto read_op = ArenaMakeShared<PgsqlReadOp>(
        arena_ptr(), &arena(), *target_, locality_info, pg_session_->metrics().metrics_capture());
    // Use the same time as PgSample. Otherwise, ybctids may be gone
    // when PgSample tries to fetch the rows.
    read_op->set_read_time(ReadHybridTime::SingleTime(read_time));
    read_req_ = std::shared_ptr<LWPgsqlReadRequestPB>(read_op, &read_op->read_request());
    doc_op_ = std::make_shared<PgDocSampleOp>(pg_session_, &target_, std::move(read_op));
    return Status::OK();
  }

  void SetSamplingState(
      const int targrows, const SampleRandomState& rand_state,
      YsqlSamplingAlgorithm ysql_sampling_algorithm) {
    auto& sampling_state = *read_req_->mutable_sampling_state();
    sampling_state.set_targrows(targrows);      // target sample size
    sampling_state.set_numrows(0);              // current number of rows selected
    sampling_state.set_samplerows(0);           // rows scanned so far
    sampling_state.set_rowstoskip(-1);          // rows to skip before selecting another
    sampling_state.set_rstate_w(rand_state.w);  // Vitter algorithm's W
    sampling_state.set_sampling_algorithm(ysql_sampling_algorithm);
    sampling_state.set_docdb_blocks_sampling_method(
        DocDbBlocksSamplingMethod(FLAGS_ysql_docdb_blocks_sampling_method));
    auto& rand = *sampling_state.mutable_rand_state();
    rand.set_s0(rand_state.s0);
    rand.set_s1(rand_state.s1);
  }
};

Result<int32_t> ReadIndexFromPgDocResult(Slice* src) {
  SCHECK(
      !VERIFY_RESULT(PgDocData::CheckedReadHeaderIsNull(src)), InternalError,
      "Entry index cannot be NULL");
  const auto index = VERIFY_RESULT(PgDocData::CheckedReadNumber<int32_t>(src));
  SCHECK_GE(index, 0, IllegalState, "Unexpected negative index");
  return index;
}

Status ReadKeyFromPgDocResult(Slice* src, KeyBuffer* buffer) {
  SCHECK(
      !VERIFY_RESULT(PgDocData::CheckedReadHeaderIsNull(src)), InternalError, "NULL not expected");
  size_t key_size = VERIFY_RESULT(PgDocData::CheckedReadNumber<uint64_t>(src));
  SCHECK_GE(src->size(), key_size, InvalidArgument, "Unexpected end of data");
  buffer->assign(src->Prefix(key_size));
  src->RemovePrefix(key_size);
  return Status::OK();
}

class SampleBlocksPicker : public SamplePickerBase {
 public:
  Status ProcessResultEntry(Slice* data) override {
    const auto index = VERIFY_RESULT(ReadIndexFromPgDocResult(data));
    // Process 1st stage (getting sample blocks) result returned from DocDB.
    // Results come as (index, lower_bound_key, upper_bound_key) tuples where index is the position
    // in the blocks reservoir of predetermined size.
    SCHECK_LT(
        index, blocks_reservoir_.size(), IllegalState, "Sample blocks reservoir index is too big");
    auto& block_at_index = blocks_reservoir_[index];
    RETURN_NOT_OK(ReadKeyFromPgDocResult(data, &block_at_index.first));
    RETURN_NOT_OK(ReadKeyFromPgDocResult(data, &block_at_index.second));
    VLOG(2) << "Sample block #" << index << ": " << AsDebugHexString(block_at_index);
    return Status::OK();
  }

  Status FetchDone() override {
    VLOG_WITH_FUNC(2) << "blocks_reservoir_.size(): " << blocks_reservoir_.size();
    if (VLOG_IS_ON(3)) {
      size_t idx = 0;
      for (const auto& sample_block : blocks_reservoir_) {
        VLOG_WITH_FUNC(3) << "Sample block #" << idx << ": " << AsDebugHexString(sample_block);
        ++idx;
      }
    }
    blocks_reservoir_ready_ = true;

    const auto num_blocks_collected = GetNumBlocksCollected();

    if (num_blocks_collected < blocks_reservoir_.size()) {
      // Sanity check - make sure all blocks after `num_blocks_collected` are empty.
      for (auto i = num_blocks_collected; i < blocks_reservoir_.size(); ++i) {
        SCHECK(
            blocks_reservoir_[i].first.empty() && blocks_reservoir_[i].second.empty(),
            InternalError,
            Format(
                "Unexpected non-empty block at index $0: $1. num_blocks_collected: $2", i,
                AsDebugHexString(blocks_reservoir_[i]), num_blocks_collected));
      }
      // If blocks reservoir is not full, blocks should be covering the whole table and we can
      // effectively replace it with single unbounded block.
      blocks_reservoir_.resize(1);
      blocks_reservoir_[0] = {};
      return Status::OK();
    }

    return Status::OK();
  }

  Result<std::vector<std::pair<KeyBuffer, KeyBuffer>>> FetchSampleBlocksBounds() {
    SCHECK(
        blocks_reservoir_ready_, IllegalState,
        "Sample blocks reservoir is not ready or already fetched");
    blocks_reservoir_ready_ = false;
    return std::move(blocks_reservoir_);
  }

  uint64_t GetNumBlocksProcessed() {
    return GetSampleOp().GetSamplingStats().num_blocks_processed;
  }

  uint64_t GetNumBlocksCollected() {
    return GetSampleOp().GetSamplingStats().num_blocks_collected;
  }

  static Result<std::unique_ptr<SampleBlocksPicker>> Make(
      const PgSession::ScopedRefPtr& pg_session, const PgObjectId& table_id,
      const YbcPgTableLocalityInfo& locality_info, int targrows,
      const SampleRandomState& rand_state, HybridTime read_time,
      YsqlSamplingAlgorithm ysql_sampling_algorithm) {
    std::unique_ptr<SampleBlocksPicker> result{new SampleBlocksPicker{pg_session}};
    RETURN_NOT_OK(result->Prepare(
        table_id, locality_info, targrows, rand_state, read_time, ysql_sampling_algorithm));
    return result;
  }

 private:
  explicit SampleBlocksPicker(const PgSession::ScopedRefPtr& pg_session)
      : SamplePickerBase(pg_session) {
  }

  Status Prepare(
      const PgObjectId& table_id, const YbcPgTableLocalityInfo& locality_info, int targrows,
      const SampleRandomState& rand_state, HybridTime read_time,
      YsqlSamplingAlgorithm ysql_sampling_algorithm) {

    RETURN_NOT_OK(
        SamplePickerBase::Prepare(table_id, locality_info, read_time));
    SetSamplingState(targrows, rand_state, ysql_sampling_algorithm);
    read_req_->mutable_sampling_state()->set_is_blocks_sampling_stage(true);

    blocks_reservoir_.reserve(targrows);
    blocks_reservoir_.insert(blocks_reservoir_.begin(), targrows, {});

    return Status::OK();
  }

  std::vector<std::pair<KeyBuffer, KeyBuffer>> blocks_reservoir_;
  bool blocks_reservoir_ready_ = false;
};

} // namespace

// Internal class to select sample rows ybctids.
class SampleRowsPicker : public SamplePickerBase, public SampleRowsPickerIf {
 public:
  Status ProcessResultEntry(Slice* data) override {
    const auto index = VERIFY_RESULT(ReadIndexFromPgDocResult(data));
    SCHECK_LT(index, rows_reservoir_.size(), IllegalState, "Rows reservoir index is too big");
    // Read ybctid column
    RETURN_NOT_OK(ReadKeyFromPgDocResult(data, &rows_reservoir_[index]));
    return Status::OK();
  }

  Status FetchDone() override {
    // Skip fetch if the table is empty.
    rows_reservoir_ready_ = true;
    return Status::OK();
  }

  Status Exec() override {
    return SamplePickerBase::Exec(/* exec_params = */ nullptr);
  }

  Result<bool> ProcessNextBlock() override {
    return SamplePickerBase::ProcessNextBlock();
  }

  Result<const std::vector<Slice>&> FetchYbctids() override {
    SCHECK(rows_reservoir_ready_, IllegalState, "Rows reservoir is not ready");

    const auto num_rows_collected = GetSampleOp().GetSamplingStats().num_rows_collected;
    if (std::cmp_less(num_rows_collected, rows_reservoir_.size())) {
      // Sanity check - make sure there are no non-empty rows `num_rows_collected` due to
      // some bug.
      for (size_t i = num_rows_collected; i < rows_reservoir_.size(); ++i) {
        SCHECK(
            rows_reservoir_[i].empty(), InternalError,
            Format(
                "Unexpected non-empty row at index $0: $1. num_rows_collected: $2", i,
                AsDebugHexString(rows_reservoir_[i]), num_rows_collected));
      }

      rows_reservoir_.resize(num_rows_collected);
    }

    ybctids_.clear();
    for (size_t i = 0; i < rows_reservoir_.size(); ++i) {
      const auto ybctid = rows_reservoir_[i].AsSlice();
      SCHECK(
          !ybctid.empty(), InternalError,
          Format(
              "Unexpected empty row at index $0, rows reservoir size: $1", i,
              rows_reservoir_.size()));
      ybctids_.push_back(ybctid);
    }

    // In the final rows sample, sort by scan order is important to calculate pg_stats.correlation
    // properly.
    // Also having keys sorted helps with utilizing disk read-ahead mechanism and improve
    // performance for reading rows that are located nearby.
    std::sort(ybctids_.begin(), ybctids_.end());

    rows_reservoir_ready_ = false;
    return ybctids_;
  }

  double GetNumRowsProcessed() const {
    return GetSampleOp().GetSamplingStats().num_rows_processed;
  }

  double GetEstimatedRowCount() const override {
    return GetSampleOp().GetSamplingStats().num_rows_processed;
  }

  Status SetSampleBlocksBounds(std::vector<std::pair<KeyBuffer, KeyBuffer>>&& sample_blocks) {
    return GetSampleOp().SetSampleBlocksBounds(std::move(sample_blocks));
  }

  static Result<std::unique_ptr<SampleRowsPicker>> Make(
      const PgSession::ScopedRefPtr& pg_session, const PgObjectId& table_id,
      const YbcPgTableLocalityInfo& locality_info,
      int targrows, const SampleRandomState& rand_state, HybridTime read_time,
      YsqlSamplingAlgorithm ysql_sampling_algorithm) {
    std::unique_ptr<SampleRowsPicker> result{new SampleRowsPicker{pg_session}};
    RETURN_NOT_OK(result->Prepare(
        table_id, locality_info, targrows, rand_state, read_time, ysql_sampling_algorithm));
    return result;
  }

 protected:
  explicit SampleRowsPicker(const PgSession::ScopedRefPtr& pg_session)
      : SamplePickerBase(pg_session) {
  }

  Status Prepare(
      const PgObjectId& table_id, const YbcPgTableLocalityInfo& locality_info, int targrows,
      const SampleRandomState& rand_state, HybridTime read_time,
      YsqlSamplingAlgorithm ysql_sampling_algorithm) {
    RETURN_NOT_OK(SamplePickerBase::Prepare(table_id, locality_info, read_time));

    SetSamplingState(targrows, rand_state, ysql_sampling_algorithm);
    read_req_->mutable_sampling_state()->set_is_blocks_sampling_stage(false);

    rows_reservoir_.reserve(targrows);
    rows_reservoir_.insert(rows_reservoir_.begin(), targrows, {});

    return Status::OK();
  }

 private:
  // The reservoir to keep ybctids of selected sample rows
  std::vector<KeyBuffer> rows_reservoir_;
  // If true sampling is completed and ybctids can be collected from the rows reservoir
  bool rows_reservoir_ready_ = false;
  // Vector of Slices pointing to the keys in the rows reservoir
  std::vector<Slice> ybctids_;
};

// Uses SampleBlocksPicker for picking blocks at first stage and SampleRowsPicker for picking rows
// at second stage.
class TwoStageSampleRowsPicker : public SampleRowsPickerIf {
 public:
  Status Exec() override {
    return sample_blocks_picker_->Exec(/* exec_params = */ nullptr);
  }

  Result<bool> ProcessNextBlock() override {
    if (sample_blocks_picker_) {
      if (!VERIFY_RESULT(sample_blocks_picker_->ProcessNextBlock())) {
        // Switch to 2nd stage - use selected sample blocks to pick rows for the final sample.
        RETURN_NOT_OK(sample_rows_picker_->SetSampleBlocksBounds(
            VERIFY_RESULT(sample_blocks_picker_->FetchSampleBlocksBounds())));
        num_blocks_processed_ = sample_blocks_picker_->GetNumBlocksProcessed();
        num_blocks_collected_ = sample_blocks_picker_->GetNumBlocksCollected();
        sample_blocks_picker_.reset();

        RETURN_NOT_OK(sample_rows_picker_->Exec());
      }
      return true;
    }
    return sample_rows_picker_->ProcessNextBlock();
  }

  double GetEstimatedRowCount() const override {
    const auto num_rows_processed = sample_rows_picker_->GetNumRowsProcessed();
    VLOG_WITH_FUNC(1) << "num_rows_processed: " << num_rows_processed
                      << " num_blocks_collected_: " << num_blocks_collected_
                      << " num_blocks_processed_: " << num_blocks_processed_;
    return num_blocks_collected_ >= num_blocks_processed_
               ? num_rows_processed
               : 1.0 * num_rows_processed * num_blocks_processed_ / num_blocks_collected_;
  }

  Result<const std::vector<Slice>&> FetchYbctids() override {
    return sample_rows_picker_->FetchYbctids();
  }

  static Result<std::unique_ptr<SampleRowsPickerIf>> Make(
      const PgSession::ScopedRefPtr& pg_session, const PgObjectId& table_id,
      const YbcPgTableLocalityInfo& locality_info,
      int targrows, const SampleRandomState& rand_state, HybridTime read_time,
      YsqlSamplingAlgorithm ysql_sampling_algorithm) {
    std::unique_ptr<TwoStageSampleRowsPicker> result{new TwoStageSampleRowsPicker{pg_session}};
    RETURN_NOT_OK(result->Prepare(
        table_id, locality_info, targrows, rand_state, read_time, ysql_sampling_algorithm));
    return result;
  }

 private:
  explicit TwoStageSampleRowsPicker(const PgSession::ScopedRefPtr& pg_session)
      : pg_session_(pg_session) {
  }

  Status Prepare(
      const PgObjectId& table_id, const YbcPgTableLocalityInfo& locality_info, int targrows,
      const SampleRandomState& rand_state, HybridTime read_time,
      YsqlSamplingAlgorithm ysql_sampling_algorithm) {
    sample_blocks_picker_ = VERIFY_RESULT(SampleBlocksPicker::Make(
        pg_session_, table_id, locality_info, targrows, rand_state, read_time,
        ysql_sampling_algorithm));
    sample_rows_picker_ = VERIFY_RESULT(SampleRowsPicker::Make(
        pg_session_, table_id, locality_info, targrows, rand_state, read_time,
        ysql_sampling_algorithm));
    return Status::OK();
  }

  PgSession::ScopedRefPtr pg_session_;
  std::unique_ptr<SampleBlocksPicker> sample_blocks_picker_;
  size_t num_blocks_processed_;
  size_t num_blocks_collected_;
  std::unique_ptr<SampleRowsPicker> sample_rows_picker_;
};

PgSample::PgSample(const PgSession::ScopedRefPtr& pg_session)
    : BaseType(pg_session) {
}

PgSample::~PgSample() {}

Status PgSample::Prepare(
    const PgObjectId& table_id, const YbcPgTableLocalityInfo& locality_info, int targrows,
    const SampleRandomState& rand_state, HybridTime read_time) {
  // Setup target and bind descriptor.
  target_ = PgTable(VERIFY_RESULT(pg_session_->LoadTable(table_id)));
  bind_ = PgTable(nullptr);

  YsqlSamplingAlgorithm ysql_sampling_algorithm;
  if (yb_allow_separate_requests_for_sampling_stages) {
    ysql_sampling_algorithm = YsqlSamplingAlgorithm(yb_sampling_algorithm);
  } else {
    // Older versions might not have ExecuteSampleBlockBased implementation which runs block-based
    // sampling stages separately. For both backward compatibility and simplicity, in this case we
    // fallback to full table scan for ANALYZE. This only affects performance of colocated tables
    // ANALYZE queries launched during upgrade process.
    ysql_sampling_algorithm = YsqlSamplingAlgorithm::FULL_TABLE_SCAN;
  }

  if (ysql_sampling_algorithm == YsqlSamplingAlgorithm::BLOCK_BASED_SAMPLING) {
    sample_rows_picker_ = VERIFY_RESULT(TwoStageSampleRowsPicker::Make(
        pg_session_, table_id, locality_info, targrows, rand_state, read_time,
        ysql_sampling_algorithm));
  } else {
    sample_rows_picker_ = VERIFY_RESULT(SampleRowsPicker::Make(
        pg_session_, table_id, locality_info, targrows, rand_state, read_time,
        ysql_sampling_algorithm));
  }

  // Prepare read op to fetch rows
  auto read_op = ArenaMakeShared<PgsqlReadOp>(
      arena_ptr(), &arena(), *target_, locality_info,
      pg_session_->metrics().metrics_capture());
  // Clamp the read uncertainty window to avoid read restart errors.
  read_op->set_read_time(ReadHybridTime::SingleTime(read_time));
  read_req_ = std::shared_ptr<LWPgsqlReadRequestPB>(read_op, &read_op->read_request());
  doc_op_ = make_shared<PgDocReadOp>(pg_session_, &target_, std::move(read_op));

  VLOG_WITH_FUNC(3)
    << "Sampling table: " << target_->table_name().table_name()
    << " for " << targrows << " rows"
    << " using read time: " << read_time;

  return sample_rows_picker_->Exec();
}

Result<bool> PgSample::SampleNextBlock() {
  const auto continue_sampling = VERIFY_RESULT(sample_rows_picker_->ProcessNextBlock());
  if (!continue_sampling) {
    ybctids_ = &VERIFY_RESULT_REF(sample_rows_picker_->FetchYbctids());
  }
  return continue_sampling;
}

EstimatedRowCount PgSample::GetEstimatedRowCount() {
  const auto estimated_total_rows = sample_rows_picker_->GetEstimatedRowCount();
  VLOG_WITH_FUNC(1) << "Returning liverows " << estimated_total_rows;
  // Postgres wants estimation of dead tuples count to trigger vacuuming, but it is unlikely it
  // will be useful for us.
  return EstimatedRowCount{.sampledrows = static_cast<int>(ybctids_->size()),
                           .live = estimated_total_rows, .dead = 0};
}

Result<std::unique_ptr<PgSample>> PgSample::Make(
    const PgSession::ScopedRefPtr& pg_session, const PgObjectId& table_id,
    const YbcPgTableLocalityInfo& locality_info,
    int targrows, const SampleRandomState& rand_state, HybridTime read_time) {
  std::unique_ptr<PgSample> result{new PgSample{pg_session}};
  RETURN_NOT_OK(result->Prepare(table_id, locality_info, targrows, rand_state, read_time));
  return result;
}

Status PgSample::SetNextBatchYbctids(const YbcPgExecParameters* exec_params) {
  DCHECK(ybctids_);
  auto& ybctids = *ybctids_;
  SCHECK_LT(index_, ybctids.size(), InternalError, "Trying to fetch more rows than expected");
  // Limits: 0 means 'unlimited'.
  uint64_t row_limit = exec_params->yb_fetch_row_limit;
  row_limit = row_limit > 0 ? row_limit : INT_MAX;
  uint64_t size_limit = exec_params->yb_fetch_size_limit;
  uint64_t max_size = FLAGS_rpc_max_message_size * FLAGS_max_buffer_size_to_rpc_limit_ratio;
  if (size_limit == 0 || size_limit > max_size)
    size_limit = max_size;

  uint64_t start_index = index_;
  // Estimate the number of ybctids based on row_limit and size_limit.
  uint64_t size = 0;
  for (; index_ < ybctids.size(); ++index_) {
    if (size + ybctids[index_].size() > size_limit || index_ - start_index >= row_limit) {
      break;
    }
    size += ybctids[index_].size();
  }
  // In case size_limit is too small, we want to fetch at least one row.
  if (start_index == index_) {
    ++index_;
  }

  // Set request with the next batch of ybctids to fetch the next batch of rows.
  SetRequestedYbctids({make_lw_function([it = ybctids.begin() + start_index,
                                         end = ybctids.begin() + index_]() mutable {
    return it != end ? *it++ : Slice();
  }), index_ - start_index});
  VLOG_WITH_FUNC(3) << "Fetching " << index_ - start_index << " sampled rows";
  return Status::OK();
}

} // namespace yb::pggate
