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

#include "yb/tablet/vector_index_checker.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <optional>

#include "yb/common/read_hybrid_time.h"

#include "yb/docdb/deadline_info.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/doc_vector_index.h"
#include "yb/docdb/key_bounds.h"

#include "yb/dockv/doc_vector_id.h"
#include "yb/dockv/pg_row.h"
#include "yb/dockv/reader_projection.h"

#include "yb/qlexpr/index.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet.pb.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/util/kv_util.h"
#include "yb/util/logging.h"
#include "yb/util/status_format.h"
#include "yb/util/uuid.h"

namespace yb::tablet {

namespace {

constexpr size_t kDefaultMaxInconsistencies = 100;

// Vector ids are random UUIDs, so they are used as integers without hashing.
using VectorIdInt = unsigned __int128;

VectorIdInt ToInt(const vector_index::VectorId& vector_id) {
  VectorIdInt result;
  static_assert(sizeof(result) == kUuidSize);
  memcpy(&result, vector_id.data(), sizeof(result));
  return result;
}

// Vector ids are spread over buckets by their low bits. A row whose vector is missing in the index
// leaves the sums of its bucket different, see FindMissingVectors. Unlike XOR, a sum does not
// cancel a duplicate.
struct VectorIdBucket {
  // Sum of the ids of the live vectors, i.e. the vectors resolved to a row referencing them.
  VectorIdInt live_vectors_sum = 0;
  // Sum of the ids of the vectors referenced by rows.
  VectorIdInt rows_sum = 0;
};

struct CollectedVector {
  vector_index::VectorId id;
  // ybctid the payload references, empty when the index stores no payload.
  KeyBuffer payload_ybctid;
};

// The checker reads the indexed table and the reverse mapping at a single hybrid time which no
// transaction still running could have written at, from the regular DB only. No intent apply
// can add data at such a time, so all the reads see the same applied state however they are
// interleaved with applies. The vector index chunks carry no hybrid times, so the index is read
// as is: a vector written after the read time resolves to no row at it and counts as
// invisible, which is also what the search path does with it.
// Compaction cannot drop a vector referenced at the read time: VectorMergeFilter drops a vector
// only when its reverse mapping entry is gone, not just tombstoned, and regular compaction removes
// a tombstone only below the history cutoff, which the read time is registered at or above.
class VectorIndexChecker {
 public:
  VectorIndexChecker(
      Tablet& tablet, docdb::DocVectorIndex& vector_index, size_t max_inconsistencies,
      CoarseTimePoint deadline)
      : tablet_(tablet), vector_index_(vector_index),
        max_inconsistencies_(
            max_inconsistencies ? max_inconsistencies : kDefaultMaxInconsistencies),
        stores_payload_(vector_index.StoresPayload()),
        has_reverse_mapping_(vector_index.HasReverseMapping()),
        deadline_(deadline), deadline_info_(deadline) {
  }

  Result<VectorIndexCheckResultPB> Run() {
    // A deactivated index lacks the rows committed after its cutoff by design.
    SCHECK(!vector_index_.IsDeactivated(), IllegalState, "Vector index is deactivated");
    SCHECK(VERIFY_RESULT(vector_index_.BackfillDone()), TryAgain,
           "Vector index backfill is in progress");

    auto read_ht = VERIFY_RESULT(tablet_.SafeTime(RequireLease::kFalse));
    if (auto* participant = tablet_.transaction_participant()) {
      auto min_running_ht = participant->MinRunningHybridTime();
      SCHECK_NE(min_running_ht, HybridTime::kInvalid, TryAgain, "Transactions are not loaded yet");
      read_ht = std::min(read_ht, min_running_ht.Decremented());
    }
    // Keeps the history at read_ht from being compacted away during the check.
    auto read_operation = ScopedReadOperation::Create(
        &tablet_, RequireLease::kFalse, ReadHybridTime::SingleTime(read_ht));
    if (!read_operation.ok()) {
      return read_operation.status().CloneAndPrepend(
          "Read time is below the history cutoff, possibly because of a long running "
          "transaction");
    }

    // Seal the vectors inserted so far into immutable chunks, the view covers only those.
    RETURN_NOT_OK(vector_index_.Flush());
    RETURN_NOT_OK(vector_index_.WaitForFlush());
    view_ = VERIFY_RESULT(vector_index_.CreateImmutableView());
    // About sqrt(N) buckets of about sqrt(N) vectors each balance the memory taken by the bucket
    // sums against the ids FindMissingVectors collects for a bucket.
    buckets_.resize(1ULL << (std::bit_width(view_->NumVectors()) / 2));
    reverse_mapping_reader_ = VERIFY_RESULT(vector_index_.context().CreateReverseMappingReader(
        ReadHybridTime::SingleTime(read_ht), nullptr));
    RETURN_NOT_OK(InitRowIterator(read_ht));

    result_.set_read_ht(read_ht.ToUint64());
    RETURN_NOT_OK(view_->ForEachVector(make_lw_function(
        [this](const docdb::DocVectorIndexEntry& entry) { return CheckVector(entry); })));
    RETURN_NOT_OK(CheckRows());
    RETURN_NOT_OK(FindMissingVectors());
    return std::move(result_);
  }

 private:
  std::string LogPrefix() const {
    return vector_index_.ToString() + ": ";
  }

  Status InitRowIterator(HybridTime read_ht) {
    auto index_table = VERIFY_RESULT(tablet_.metadata()->GetTableInfo(vector_index_.table_id()));
    const auto& indexed_table_id = index_table->index_info->indexed_table_id();
    auto indexed_table = VERIFY_RESULT(tablet_.metadata()->GetTableInfo(indexed_table_id));
    projection_.emplace(indexed_table->schema(), std::array{vector_index_.column_id()});
    SCHECK_EQ(projection_->num_value_columns(), 1, IllegalState,
              "Vector column is missing in the projection");
    row_.emplace(*projection_);
    row_iter_ = VERIFY_RESULT(tablet_.NewUninitializedDocRowIterator(
        *projection_, ReadHybridTime::SingleTime(read_ht), indexed_table_id,
        deadline_, AllowBootstrappingState::kFalse, docdb::IncludeIntents::kFalse));
    // Every read positions the iterator itself, see SeekRow.
    return row_iter_->InitForTableType(
        TableType::PGSQL_TABLE_TYPE, Slice(), docdb::SkipSeek::kTrue,
        docdb::AddTablePrefixToKey::kTrue);
  }

  void SeekRow(Slice ybctid) {
    row_iter_->Refresh(docdb::SeekFilter::kAll);
    // SeekTuple prepends the cotable / colocation prefix to the key.
    row_iter_->SeekTuple(ybctid, docdb::UpdateFilterKey::kFalse);
  }

  // Returns false after the last row.
  Result<bool> FetchNextRow() {
    if (!VERIFY_RESULT(row_iter_->PgFetchNext(&*row_))) {
      return false;
    }
    auto value = row_->GetValueByIndex(0);
    row_vector_ = value ? value->binary_value() : Slice();
    return true;
  }

  // Returns false when the row does not exist. The vector of a fetched row could be NULL.
  Result<bool> FetchRow(Slice ybctid) {
    SeekRow(ybctid);
    return VERIFY_RESULT(FetchNextRow()) && row_iter_->GetTupleId() == ybctid;
  }

  // Skips rows with a NULL vector, returns false after the last row.
  Result<bool> FetchNextRowWithVector() {
    while (VERIFY_RESULT(FetchNextRow())) {
      if (!row_vector_.empty()) {
        return true;
      }
    }
    return false;
  }

  Result<vector_index::VectorId> RowVectorId() const {
    return dockv::EncodedDocVectorValue::FromSlice(row_vector_).DecodeId();
  }

  size_t BucketIndex(const vector_index::VectorId& vector_id) const {
    return static_cast<size_t>(ToInt(vector_id)) & (buckets_.size() - 1);
  }

  // Returns the ybctid of the row the vector belongs to, empty when the vector is invisible.
  Result<Slice> VectorYbctid(const docdb::DocVectorIndexEntry& entry) {
    if (!stores_payload_) {
      return reverse_mapping_reader_->FetchYbctid(entry.vector_id);
    }
    auto ybctid = VERIFY_RESULT(dockv::DocVectorIndexPayloadYbctid(entry.payload));
    // Rows of the sibling tablet are inherited by a split and dropped by the post split
    // compaction, see VectorMergeFilter.
    return tablet_.key_bounds().IsWithinBounds(ybctid) ? ybctid : Slice();
  }

  // Every vector of the index belongs to a row referencing it, or is invisible.
  Status CheckVector(const docdb::DocVectorIndexEntry& entry) {
    RETURN_NOT_OK(deadline_info_.CheckDeadlinePassed());
    result_.set_num_vectors(result_.num_vectors() + 1);
    auto ybctid = VERIFY_RESULT(VectorYbctid(entry));
    if (ybctid.empty()) {
      return Invisible();
    }
    if (!VERIFY_RESULT(FetchRow(ybctid))) {
      // A payload proves nothing about the row, the reverse mapping entry is tombstoned with the
      // row.
      if (stores_payload_) {
        return Invisible();
      }
      Add(VectorIndexInconsistencyKind::SPURIOUS_REVERSE_MAPPING, entry.vector_id, std::nullopt,
          ybctid, "row does not exist");
      return Status::OK();
    }
    auto row_value = dockv::EncodedDocVectorValue::FromSlice(row_vector_);
    if (row_vector_.empty() || row_value.id != entry.vector_id.AsSlice()) {
      if (stores_payload_) {
        return Invisible();
      }
      Add(VectorIndexInconsistencyKind::SPURIOUS_REVERSE_MAPPING, entry.vector_id, ybctid, ybctid,
          row_vector_.empty()
              ? "row vector is NULL"
              : Format("row references vector $0", row_value.id.ToDebugHexString()));
      return Status::OK();
    }
    buckets_[BucketIndex(entry.vector_id)].live_vectors_sum += ToInt(entry.vector_id);
    if (row_value.data != entry.vector) {
      Add(VectorIndexInconsistencyKind::VECTOR_MISMATCH, entry.vector_id, ybctid, ybctid, "");
    }
    return Status::OK();
  }

  // Adds the vector id of every row with a vector to its bucket, see FindMissingVectors. When the
  // table writes the reverse mapping, also checks that the vector has a live entry pointing back
  // to the row.
  Status CheckRows() {
    SeekRow(Slice());
    while (VERIFY_RESULT(FetchNextRowWithVector())) {
      result_.set_num_rows(result_.num_rows() + 1);
      auto vector_id = VERIFY_RESULT(RowVectorId());
      buckets_[BucketIndex(vector_id)].rows_sum += ToInt(vector_id);
      if (!has_reverse_mapping_) {
        continue;
      }
      auto ybctid = row_iter_->GetTupleId();
      auto mapped_ybctid = VERIFY_RESULT(reverse_mapping_reader_->FetchYbctid(vector_id));
      if (mapped_ybctid.empty()) {
        Add(VectorIndexInconsistencyKind::MISSING_REVERSE_MAPPING, vector_id, ybctid,
            std::nullopt, "");
      } else if (mapped_ybctid != ybctid) {
        Add(VectorIndexInconsistencyKind::REVERSE_MAPPING_MISMATCH, vector_id, ybctid,
            mapped_ybctid, "");
      }
    }
    return Status::OK();
  }

  // The sums of a bucket differ when a row references a vector missing in the index, one with
  // several live copies in the index, or one in the index but not live: without its reverse
  // mapping entry, which CheckRows reports, or with a payload referencing another row. To keep at
  // most one bucket of ids in memory, each such bucket takes its own pass, collecting the ids of
  // its vectors and looking its rows up among them. Every such bucket has an inconsistency
  // reported here or by CheckRows, so max_inconsistencies bounds the number of passes.
  Status FindMissingVectors() {
    for (size_t bucket = 0; bucket != buckets_.size() && !result_.truncated(); ++bucket) {
      if (buckets_[bucket].live_vectors_sum != buckets_[bucket].rows_sum) {
        RETURN_NOT_OK(FindMissingVectors(bucket));
      }
    }
    return Status::OK();
  }

  Status FindMissingVectors(size_t bucket) {
    collected_vectors_.clear();
    RETURN_NOT_OK(view_->ForEachVector(make_lw_function(
        [this, bucket](const docdb::DocVectorIndexEntry& entry) {
          return CollectVector(entry, bucket);
        })));
    std::ranges::sort(collected_vectors_, {}, &CollectedVector::id);
    SeekRow(Slice());
    while (VERIFY_RESULT(FetchNextRowWithVector())) {
      auto vector_id = VERIFY_RESULT(RowVectorId());
      if (BucketIndex(vector_id) != bucket) {
        continue;
      }
      auto ybctid = row_iter_->GetTupleId();
      auto copies = std::ranges::equal_range(
          collected_vectors_, vector_id, {}, &CollectedVector::id);
      if (copies.empty()) {
        Add(VectorIndexInconsistencyKind::MISSING_VECTOR, vector_id, ybctid, std::nullopt, "");
        continue;
      }
      if (copies.size() > 1) {
        Add(VectorIndexInconsistencyKind::DUPLICATE_VECTOR, vector_id, ybctid, std::nullopt,
            Format("$0 copies", copies.size()));
      }
      if (stores_payload_ && std::ranges::none_of(copies, [ybctid](const auto& copy) {
            return copy.payload_ybctid.AsSlice() == ybctid;
          })) {
        Add(VectorIndexInconsistencyKind::PAYLOAD_MISMATCH, vector_id, ybctid,
            copies.front().payload_ybctid.AsSlice(), "");
      }
    }
    return Status::OK();
  }

  Status CollectVector(const docdb::DocVectorIndexEntry& entry, size_t bucket) {
    RETURN_NOT_OK(deadline_info_.CheckDeadlinePassed());
    if (BucketIndex(entry.vector_id) != bucket) {
      return Status::OK();
    }
    auto& collected = collected_vectors_.emplace_back();
    collected.id = entry.vector_id;
    if (stores_payload_) {
      collected.payload_ybctid.Assign(
          VERIFY_RESULT(dockv::DocVectorIndexPayloadYbctid(entry.payload)));
    }
    return Status::OK();
  }

  Status Invisible() {
    result_.set_num_invisible_vectors(result_.num_invisible_vectors() + 1);
    return Status::OK();
  }

  void Add(
      VectorIndexInconsistencyKind kind, const vector_index::VectorId& vector_id,
      std::optional<Slice> ybctid, std::optional<Slice> index_ybctid, const std::string& details) {
    if (static_cast<size_t>(result_.inconsistencies_size()) >= max_inconsistencies_) {
      result_.set_truncated(true);
      return;
    }
    LOG_WITH_PREFIX(WARNING)
        << "Vector index inconsistency: " << VectorIndexInconsistencyKind_Name(kind)
        << ", vector id: " << vector_id
        << ", ybctid: " << (ybctid ? ybctid->ToDebugHexString() : "<none>")
        << ", index ybctid: " << (index_ybctid ? index_ybctid->ToDebugHexString() : "<none>")
        << (details.empty() ? "" : ", " + details);
    auto& out = *result_.add_inconsistencies();
    out.set_kind(kind);
    out.set_vector_id(vector_id.AsSlice().cdata(), vector_id.AsSlice().size());
    if (ybctid) {
      out.set_ybctid(ybctid->cdata(), ybctid->size());
    }
    if (index_ybctid) {
      out.set_index_ybctid(index_ybctid->cdata(), index_ybctid->size());
    }
    if (!details.empty()) {
      out.set_details(details);
    }
  }

  Tablet& tablet_;
  docdb::DocVectorIndex& vector_index_;
  const size_t max_inconsistencies_;
  const bool stores_payload_;
  const bool has_reverse_mapping_;
  const CoarseTimePoint deadline_;
  docdb::DeadlineInfo deadline_info_;

  docdb::DocVectorIndexViewPtr view_;
  docdb::DocVectorIndexReverseMappingReaderPtr reverse_mapping_reader_;
  // The row and the iterator reference the projection.
  std::optional<dockv::ReaderProjection> projection_;
  std::optional<dockv::PgTableRow> row_;
  docdb::DocRowwiseIteratorPtr row_iter_;
  // Vector of the fetched row, empty when it is NULL. Points into row_.
  Slice row_vector_;
  std::vector<VectorIdBucket> buckets_;
  std::vector<CollectedVector> collected_vectors_;
  VectorIndexCheckResultPB result_;
};

} // namespace

Result<VectorIndexCheckResultPB> CheckVectorIndex(
    Tablet& tablet, docdb::DocVectorIndex& vector_index, size_t max_inconsistencies,
    CoarseTimePoint deadline) {
  return VectorIndexChecker(tablet, vector_index, max_inconsistencies, deadline).Run();
}

}  // namespace yb::tablet
