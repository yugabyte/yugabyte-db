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

#include "yb/docdb/doc_read_context.h"

#include "yb/common/ql_type.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/value_type.h"

#include "yb/util/logging.h"
#include "yb/util/locks.h"

namespace yb::docdb {

DocReadContext::DocReadContext(
    const std::string& log_prefix, TableType table_type, Index is_index_,
    dockv::SchemaPackingRegistryPtr registry)
    : is_index(is_index_),
      schema_packing_storage(table_type, std::move(registry)),
      log_prefix_(log_prefix) {
  UpdateKeyPrefix();
}

DocReadContext::DocReadContext(
    const std::string& log_prefix, TableType table_type, Index is_index_,
    dockv::SchemaPackingRegistryPtr registry, const Schema& schema, SchemaVersion schema_version)
    : is_index(is_index_),
      schema_packing_storage(table_type, std::move(registry)),
      schema_(schema),
      log_prefix_(log_prefix) {
  schema_packing_storage.AddSchema(schema_version, schema_);
  UpdateKeyPrefix();
  LOG_IF_WITH_PREFIX(INFO, schema_version != 0)
      << "DocReadContext, from schema, version: " << schema_version;
}

DocReadContext::DocReadContext(const DocReadContext& rhs)
    : is_index(rhs.is_index),
      schema_packing_storage(rhs.schema_packing_storage),
      vector_idx_options(rhs.vector_idx_options),
      schema_(rhs.schema_),
      log_prefix_(rhs.log_prefix_) {
  // Intentionally leave tombstone-cache fields at defaults (unarmed / uncached). Same-schema-
  // version TableInfo rebuilds use this ctor; carrying a warm cache would break fail-closed.
  UpdateKeyPrefix();
  VLOG_WITH_PREFIX(1) << "DocReadContext, copy (cache reset)";
}

DocReadContext::DocReadContext(
    const DocReadContext& rhs, const Schema& schema, SchemaVersion schema_version)
    : is_index(rhs.is_index),
      schema_packing_storage(rhs.schema_packing_storage),
      schema_(schema),
      log_prefix_(rhs.log_prefix_) {
  schema_packing_storage.AddSchema(schema_version, schema_);
  UpdateKeyPrefix();
  LOG_WITH_PREFIX(INFO)
      << "DocReadContext, copy and add: " << schema_packing_storage.VersionsToString()
      << ", added: " << schema_version;
}

DocReadContext::DocReadContext(const DocReadContext& rhs, const Schema& schema)
    : is_index(rhs.is_index),
      schema_packing_storage(rhs.schema_packing_storage),
      schema_(schema),
      log_prefix_(rhs.log_prefix_) {
  UpdateKeyPrefix();
  LOG_WITH_PREFIX(INFO) << "DocReadContext, copy and replace schema";
}

DocReadContext::DocReadContext(const DocReadContext& rhs, SchemaVersion min_schema_version)
    : is_index(rhs.is_index),
      schema_packing_storage(rhs.schema_packing_storage, min_schema_version),
      schema_(rhs.schema_),
      log_prefix_(rhs.log_prefix_) {
  UpdateKeyPrefix();
  LOG_WITH_PREFIX(INFO)
      << "DocReadContext, copy and filter: " << rhs.schema_packing_storage.VersionsToString()
      << " => " << schema_packing_storage.VersionsToString() << ", min_schema_version: "
      << min_schema_version;
}

std::optional<DocHybridTime> DocReadContext::table_tombstone_time() const {
  std::lock_guard lock(tombstone_cache_mutex_);
  if (table_tombstone_time_ == DocHybridTime::kMax ||
      tombstone_cache_entry_generation_ != tombstone_cache_generation_) {
    return std::nullopt; // Not yet cached, or invalidated by a watermark advance.
  }
  return table_tombstone_time_;
}

void DocReadContext::set_table_tombstone_time(
    DocHybridTime table_tombstone_time, uint64_t entry_generation) const {
  DCHECK(schema_.has_colocation_id());
  std::lock_guard lock(tombstone_cache_mutex_);
  // Reject a populate that raced a truncate: its entry_generation is from before the bump.
  if (entry_generation != tombstone_cache_generation_) {
    return;
  }
  // A cached tombstone must not sit above the watermark. Otherwise a concurrent read with
  // watermark <= read_ht < tombstone_ht remains eligible, hits this entry, and hides every row
  // that predates the truncate (commit-to-apply window second polarity). Absence (kInvalid) has
  // no hybrid time to compare.
  if (table_tombstone_time.is_valid() &&
      (tombstone_cache_watermark_ == HybridTime::kMax ||
       table_tombstone_time.hybrid_time() > tombstone_cache_watermark_)) {
    return;
  }
  // Both fields under the same lock so readers never observe a stale value paired with the
  // current generation (the two-store race without the lock).
  tombstone_cache_entry_generation_ = entry_generation;
  table_tombstone_time_ = table_tombstone_time;
}

void DocReadContext::clear_table_tombstone_time() const {
  std::lock_guard lock(tombstone_cache_mutex_);
  table_tombstone_time_ = DocHybridTime::kMax;
}

HybridTime DocReadContext::tombstone_cache_watermark() const {
  std::lock_guard lock(tombstone_cache_mutex_);
  return tombstone_cache_watermark_;
}

uint64_t DocReadContext::tombstone_cache_generation() const {
  std::lock_guard lock(tombstone_cache_mutex_);
  return tombstone_cache_generation_;
}

void DocReadContext::AdvanceTombstoneCacheWatermark(HybridTime ht) const {
  DCHECK(ht.is_valid());
  DCHECK_NE(ht, HybridTime::kMax);
  // kMin would make every read eligible; require a real HT so unarmed fails closed by construction.
  DCHECK_GE(ht, HybridTime::kInitial);
  std::lock_guard lock(tombstone_cache_mutex_);
  // kMax is the unarmed sentinel, not a comparable upper bound: replace it on first advance.
  // Only bump generation when the watermark actually moves, so arming/re-arming with an
  // equal-or-older SafeTime does not spuriously drop a warm cache.
  if (tombstone_cache_watermark_ == HybridTime::kMax || ht > tombstone_cache_watermark_) {
    ++tombstone_cache_generation_;
    tombstone_cache_watermark_ = ht;
  }
}

void DocReadContext::OnTableTombstoneWritten(HybridTime write_ht) const {
  DCHECK(write_ht.is_valid());
  DCHECK_NE(write_ht, HybridTime::kMax);
  DCHECK_GE(write_ht, HybridTime::kInitial);
  std::lock_guard lock(tombstone_cache_mutex_);
  // Always bump generation and clear, even when write_ht <= watermark (e.g. a post-WriteToRocksDB
  // re-notify on the xCluster external-intents path). Clearing alone is not enough if a concurrent
  // populate already decided to store under the current generation; the bump forces that entry to
  // miss. When write_ht is higher, also raise the watermark.
  ++tombstone_cache_generation_;
  if (tombstone_cache_watermark_ == HybridTime::kMax || write_ht > tombstone_cache_watermark_) {
    tombstone_cache_watermark_ = write_ht;
  }
  table_tombstone_time_ = DocHybridTime::kMax;
}

bool DocReadContext::IsTombstoneCacheEligible(HybridTime read_ht) const {
  if (!read_ht.is_valid()) {
    return false;
  }
  std::lock_guard lock(tombstone_cache_mutex_);
  // Reject unarmed watermark (kMax): kMax.is_valid() is true and read_ht >= kMax would otherwise
  // make an unarmed context eligible (e.g. ReadHybridTime::Max()).
  return tombstone_cache_watermark_ != HybridTime::kMax &&
         read_ht >= tombstone_cache_watermark_;
}

std::optional<DocHybridTime> DocReadContext::GetCachedTableTombstoneTime(
    HybridTime read_ht) const {
  if (!read_ht.is_valid()) {
    return std::nullopt;
  }
  std::lock_guard lock(tombstone_cache_mutex_);
  // Same eligibility gate as IsTombstoneCacheEligible, then the hit check from
  // table_tombstone_time - under one lock so a concurrent OnTableTombstoneWritten
  // cannot invalidate between the two.
  if (tombstone_cache_watermark_ == HybridTime::kMax ||
      read_ht < tombstone_cache_watermark_) {
    return std::nullopt;
  }
  if (table_tombstone_time_ == DocHybridTime::kMax ||
      tombstone_cache_entry_generation_ != tombstone_cache_generation_) {
    return std::nullopt;
  }
  return table_tombstone_time_;
}

void DocReadContext::LogAfterLoad() {
  if (schema_packing_storage.SingleSchemaVersion() == 0) {
    return;
  }
  LOG_WITH_PREFIX(INFO) << __func__ << ": " << schema_packing_storage.VersionsToString();
}

void DocReadContext::LogAfterMerge(dockv::OverwriteSchemaPacking overwrite) {
  LOG_WITH_PREFIX(INFO)
      << __func__ << ": " << schema_packing_storage.VersionsToString() << ", overwrite: "
      << overwrite;
}

void DocReadContext::SetCotableId(const Uuid& cotable_id) {
  schema_.set_cotable_id(cotable_id);
  UpdateKeyPrefix();
}

void DocReadContext::UpdateKeyPrefix() {
  uint8_t* out = shared_key_prefix_buffer_.data();
  if (schema_.has_cotable_id()) {
    *out++ = dockv::KeyEntryTypeAsChar::kTableId;
    schema_.cotable_id().EncodeToComparable(out);
    out += kUuidSize;
  }
  if (schema_.has_colocation_id()) {
    *out++ = dockv::KeyEntryTypeAsChar::kColocationId;
    BigEndian::Store32(out, schema_.colocation_id());
    out += sizeof(ColocationId);
  }
  key_prefix_encoded_len_ = table_key_prefix_len_ = out - shared_key_prefix_buffer_.data();
  bool use_inplace_increment_for_upperbound = false;
  if (schema_.num_hash_key_columns()) {
    *out++ = dockv::KeyEntryTypeAsChar::kUInt16Hash;
    use_inplace_increment_for_upperbound = true;
    key_prefix_encoded_len_ += 1 + sizeof(uint16_t);
  } else if (schema_.num_key_columns() && out == shared_key_prefix_buffer_.data() &&
             !is_index && schema_.columns()[0].kind() == ColumnKind::RANGE_ASC_NULL_FIRST) {
    // TODO support all known combinations of data types for first range column.
    // Currently we start only with this restricted case to be able to filter out cotable entries
    // from sys catalog.
    switch (schema_.columns()[0].type()->main()) {
      case DataType::INT32: [[fallthrough]];
      case DataType::INT16: [[fallthrough]];
      case DataType::INT8:
        *out++ = dockv::KeyEntryTypeAsChar::kInt32;
        use_inplace_increment_for_upperbound = true;
        break;
      default:
        break;
    }
  }
  shared_key_prefix_len_ = out - shared_key_prefix_buffer_.data();
  upperbound_len_ = shared_key_prefix_len_;
  memcpy(upperbound_buffer_.data(), shared_key_prefix_buffer_.data(), upperbound_len_);
  if (use_inplace_increment_for_upperbound) {
    ++upperbound_buffer_[upperbound_len_ - 1];
  } else {
    upperbound_buffer_[upperbound_len_++] = dockv::KeyEntryTypeAsChar::kHighest;
  }
}

Result<std::optional<Slice>> DocReadContext::UserKeyForFixedBloomFilter(
    Slice lower, Slice upper) const {
  if (lower.empty() ||
      !VERIFY_RESULT(dockv::HashedOrFirstRangeComponentsExistAndEqual(lower, upper))) {
    return std::nullopt;
  }
  return lower;
}

size_t DocReadContext::NumColumnsUsedByBloomFilterKey() const {
  // If there are hash columns, when we include hash code, otherwise bloom filter
  // pick the first range component.
  // So num columns used by bloom filter always num hash columns + 1.
  return schema_.num_hash_key_columns() + 1;
}

dockv::VectorValueFormat DocReadContext::vector_value_format() const {
  return schema_.table_properties().owns_vector_reverse_mapping()
      ? dockv::VectorValueFormat::kTyped : dockv::VectorValueFormat::kLegacy;
}

DocReadContext DocReadContext::TEST_Create(const Schema& schema) {
  static const auto registry = std::make_shared<dockv::SchemaPackingRegistry>("TEST: ");
  return DocReadContext(
      "TEST: ", TableType::YQL_TABLE_TYPE, Index::kFalse, registry, schema, 0);
}

} // namespace yb::docdb
