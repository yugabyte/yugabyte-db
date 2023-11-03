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

#include "yb/docdb/consensus_frontier.h"

#include <google/protobuf/any.pb.h>

#include "yb/docdb/docdb.pb.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/stl_util.h"

#include "yb/util/tostring.h"

namespace yb {
namespace docdb {

namespace {

template <class T>
void MakeAtLeast(const T& other_value, T* this_value) {
  this_value->MakeAtLeast(other_value);
}

template <class T>
void MakeAtMost(const T& other_value, T* this_value) {
  this_value->MakeAtMost(other_value);
}

template <class T>
void MakeAtLeast(const std::optional<T>& other_value, std::optional<T>* this_value) {
  if (other_value && (!*this_value || **this_value < *other_value)) {
    *this_value = *other_value;
  }
}

template <class T>
void MakeAtMost(const std::optional<T>& other_value, std::optional<T>* this_value) {
  if (other_value && (!*this_value || **this_value > *other_value)) {
    *this_value = *other_value;
  }
}

void MakeAtLeast(uint32_t other_value, uint32_t* this_value) {
  *this_value = std::max(*this_value, other_value);
}

void MakeAtMost(uint32_t other_value, uint32_t* this_value) {
  *this_value = std::min(*this_value, other_value);
}

} // namespace

ConsensusFrontier::~ConsensusFrontier() = default;

bool ConsensusFrontier::Equals(const UserFrontier& pre_rhs) const {
  const ConsensusFrontier& rhs = down_cast<const ConsensusFrontier&>(pre_rhs);
  return op_id_ == rhs.op_id_ &&
         hybrid_time_ == rhs.hybrid_time_ &&
         history_cutoff_ == rhs.history_cutoff_ &&
         hybrid_time_filter_ == rhs.hybrid_time_filter_ &&
         max_value_level_ttl_expiration_time_ == rhs.max_value_level_ttl_expiration_time_ &&
         primary_schema_version_ == rhs.primary_schema_version_ &&
         cotable_schema_versions_ == rhs.cotable_schema_versions_;
}

void ConsensusFrontier::ToPB(google::protobuf::Any* any) const {
  ConsensusFrontierPB pb;
  op_id_.ToPB(pb.mutable_op_id());
  pb.set_hybrid_time(hybrid_time_.ToUint64());
  pb.set_primary_cutoff_ht(history_cutoff_.primary_cutoff_ht.ToUint64());
  pb.set_cotables_cutoff_ht(history_cutoff_.cotables_cutoff_ht.ToUint64());
  int64_t ht = ExtractGlobalFilter(hybrid_time_filter_.AsSlice());
  // Global HT filter can be invalid in two cases:
  // 1. when only the per db filters then the first 8 bytes of hybrid_time_filter_ are invalid
  // 2. when the entire hybrid_time_filter_ is empty
  if (HybridTime::FromPB(ht).is_valid()) {
    pb.set_hybrid_time_filter(ht);
  }
  pb.set_max_value_level_ttl_expiration_time(max_value_level_ttl_expiration_time_.ToUint64());
  if (primary_schema_version_) {
    AddTableSchemaVersion(Uuid::Nil(), *primary_schema_version_, &pb);
  }
  for (const auto& p : cotable_schema_versions_) {
    AddTableSchemaVersion(p.first, p.second, &pb);
  }
  IterateCotablesFilter(hybrid_time_filter_.AsSlice(), [&pb](uint32_t db_oid, uint64_t ht) {
    auto* db_filter = pb.mutable_db_oid_to_ht_filter()->Add();
    db_filter->set_db_oid(db_oid);
    db_filter->set_ht_filter(ht);
  });
  VLOG(3) << "ConsensusFrontierPB: " << pb.ShortDebugString();
  any->PackFrom(pb);
}

Status ConsensusFrontier::FromPB(const google::protobuf::Any& any) {
  ConsensusFrontierPB pb;
  if (!any.UnpackTo(&pb)) {
    return STATUS(Corruption, "Unable to unpack consensus frontier");
  }
  op_id_ = OpId::FromPB(pb.op_id());
  hybrid_time_ = HybridTime(pb.hybrid_time());
  if (pb.has_primary_cutoff_ht()) {
    history_cutoff_.primary_cutoff_ht =
        NormalizeHistoryCutoff(HybridTime(pb.primary_cutoff_ht()));
  }
  if (pb.has_cotables_cutoff_ht()) {
    history_cutoff_.cotables_cutoff_ht =
        NormalizeHistoryCutoff(HybridTime(pb.cotables_cutoff_ht()));
  }
  max_value_level_ttl_expiration_time_ =
      HybridTime::FromPB(pb.max_value_level_ttl_expiration_time());
  for (const auto& p : pb.table_schema_version()) {
    if (p.table_id().empty()) {
      primary_schema_version_ = p.schema_version();
    } else {
      cotable_schema_versions_.emplace(
          VERIFY_RESULT(Uuid::FromSlice(p.table_id())), p.schema_version());
    }
  }
  // See the declaration of hybrid_time_filter_ to understand its layout.
  hybrid_time_filter_.clear();
  // If global filter is not present then we might still need to append invalid hybrid time
  // if cotables filter is present.
  if (pb.has_hybrid_time_filter()) {
    AppendGlobalFilter(pb.hybrid_time_filter());
  } else if (!pb.db_oid_to_ht_filter().empty()) {
    AppendGlobalFilter(HybridTime::kInvalid.ToUint64());
  }
  // First append all the db oids.
  for (const auto& per_db_filter : pb.db_oid_to_ht_filter()) {
    AppendDbOidToCotablesFilter(per_db_filter.db_oid());
  }
  // Next append all the hybrid times.
  for (const auto& per_db_filter : pb.db_oid_to_ht_filter()) {
    AppendHybridTimeToCotablesFilter(per_db_filter.ht_filter());
  }
  VLOG(3) << "ConsensusFrontier: " << ToString();
  return Status::OK();
}

HybridTime ConsensusFrontier::GlobalFilter() const {
  return HybridTime::FromPB(ExtractGlobalFilter(hybrid_time_filter_.AsSlice()));
}

void ConsensusFrontier::FromOpIdPBDeprecated(const OpIdPB& pb) {
  op_id_ = OpId::FromPB(pb);
}

std::string ConsensusFrontier::ToString() const {
  auto global_ht = GlobalFilter();
  std::string filter = Format("Global filter: $0, ", global_ht);
  filter += "Cotables filter: [";
  IterateCotablesFilter(hybrid_time_filter_.AsSlice(), [&filter](uint32_t db_oid, uint64_t ht) {
    filter += Format("$0:$1, ", db_oid, HybridTime(ht));
  });
  filter += "]";
  return filter + YB_CLASS_TO_STRING(
      op_id, hybrid_time, history_cutoff, max_value_level_ttl_expiration_time,
      primary_schema_version, cotable_schema_versions);
}

void ConsensusFrontier::SetCoTablesFilter(
    std::vector<std::pair<uint32_t, HybridTime>> db_oid_to_ht_filter) {
  // Filter is always sorted by db oid for a faster search.
  std::sort(db_oid_to_ht_filter.begin(), db_oid_to_ht_filter.end());
  // Preserve the current global filter.
  if (!hybrid_time_filter_.empty()) {
    DCHECK_GE(hybrid_time_filter_.size(), HybridTime::SizeOfHybridTimeRepr);
    hybrid_time_filter_.Truncate(HybridTime::SizeOfHybridTimeRepr);
  } else {
    AppendGlobalFilter(HybridTime::kInvalid.ToUint64());
  }
  // First append all the db oids.
  for (const auto& per_db_filter : db_oid_to_ht_filter) {
    AppendDbOidToCotablesFilter(per_db_filter.first);
  }
  // Next append all the hybrid times.
  for (const auto& per_db_filter : db_oid_to_ht_filter) {
    AppendHybridTimeToCotablesFilter(per_db_filter.second.ToUint64());
  }
}

void ConsensusFrontier::SetGlobalFilter(HybridTime value) {
  if (hybrid_time_filter_.empty()) {
    AppendGlobalFilter(value.ToUint64());
    return;
  }
  DCHECK_GE(hybrid_time_filter_.size(), HybridTime::SizeOfHybridTimeRepr);
  uint64_t ht = value.ToUint64();
  memcpy(hybrid_time_filter_.mutable_data(), pointer_cast<uint8*>(&ht), sizeof(ht));
}

uint64_t ExtractGlobalFilter(Slice filter) {
  if (filter.empty()) {
    return HybridTime::kInvalid.ToUint64();
  }
  DCHECK_GE(filter.size(), HybridTime::SizeOfHybridTimeRepr);
  uint64_t ht;
  memcpy(&ht, filter.data(), sizeof(ht));
  return ht;
}

void IterateCotablesFilter(
    Slice filter, const std::function<void(uint32_t, uint64_t)>& callback) {
  // The first 8 bytes (if any) is the global hybrid time filter.
  // Every per db entry has 4 bytes of db_oid and 8 bytes of Hybrid Time.
  const uint8* input = filter.data() + HybridTime::SizeOfHybridTimeRepr;
  // See the declaration of hybrid_time_filter_ to understand its layout.
  size_t num_filters = (filter.size() - HybridTime::SizeOfHybridTimeRepr) / kSizePerDbFilter;
  const uint8* db_oid_ptr = input;
  const uint8* ht_ptr = input + (kSizeDbOid * num_filters);
  while (ht_ptr < filter.end()) {
    uint32_t db_oid;
    memcpy(&db_oid, db_oid_ptr, sizeof(db_oid));
    db_oid_ptr += sizeof(db_oid);
    uint64_t ht;
    memcpy(&ht, ht_ptr, sizeof(ht));
    ht_ptr += sizeof(ht);
    callback(db_oid, ht);
  }
}

namespace {

// Check if the given updated value is a correct "update" for the given previous value in the
// specified direction. If one of the two values is not defined according to the bool conversion,
// then there is no error.
template<typename T>
bool IsUpdateValidForField(
    const T& this_value, const T& updated_value, rocksdb::UpdateUserValueType update_type) {
  if (!this_value || !updated_value) {
    // If any of the two values is undefined, we don't treat this as an error.
    return true;
  }
  switch (update_type) {
    case rocksdb::UpdateUserValueType::kLargest:
      return updated_value >= this_value;
    case rocksdb::UpdateUserValueType::kSmallest:
      return updated_value <= this_value;
  }
  FATAL_INVALID_ENUM_VALUE(rocksdb::UpdateUserValueType, update_type);
}

template<typename T>
void UpdateField(
    T* this_value, const T& new_value, rocksdb::UpdateUserValueType update_type) {
  switch (update_type) {
    case rocksdb::UpdateUserValueType::kLargest:
      MakeAtLeast(new_value, this_value);
      return;
    case rocksdb::UpdateUserValueType::kSmallest:
      MakeAtMost(new_value, this_value);
      return;
  }
  FATAL_INVALID_ENUM_VALUE(rocksdb::UpdateUserValueType, update_type);
}

} // anonymous namespace

void ConsensusFrontier::Update(
    const rocksdb::UserFrontier& pre_rhs, rocksdb::UpdateUserValueType update_type) {
  const ConsensusFrontier& rhs = down_cast<const ConsensusFrontier&>(pre_rhs);
  UpdateField(&op_id_, rhs.op_id_, update_type);
  UpdateField(&hybrid_time_, rhs.hybrid_time_, update_type);
  UpdateField(
      &history_cutoff_.primary_cutoff_ht, rhs.history_cutoff_.primary_cutoff_ht, update_type);
  UpdateField(&history_cutoff_.cotables_cutoff_ht,
              rhs.history_cutoff_.cotables_cutoff_ht, update_type);
  // Reset filters after compaction.
  hybrid_time_filter_.clear();
  UpdateField(&max_value_level_ttl_expiration_time_,
              rhs.max_value_level_ttl_expiration_time_, update_type);
  UpdateField(&primary_schema_version_, rhs.primary_schema_version_, update_type);
  for (const auto& p : rhs.cotable_schema_versions_) {
    auto it = cotable_schema_versions_.find(p.first);
    if (it == cotable_schema_versions_.end()) {
      cotable_schema_versions_.emplace(p);
    } else {
      UpdateField(&it->second, p.second, update_type);
    }
  }
}

Slice ConsensusFrontier::FilterAsSlice() {
  return hybrid_time_filter_.AsSlice();
}

void ConsensusFrontier::CotablesFilter(
    std::vector<std::pair<uint32_t, HybridTime>>* cotables_filter) {
  IterateCotablesFilter(hybrid_time_filter_.AsSlice(),
      [cotables_filter](uint32_t db_oid, uint64_t ht) {
        cotables_filter->emplace_back(db_oid, ht);
      }
  );
}

bool ConsensusFrontier::IsUpdateValid(
    const rocksdb::UserFrontier& pre_rhs, rocksdb::UpdateUserValueType update_type) const {
  const ConsensusFrontier& rhs = down_cast<const ConsensusFrontier&>(pre_rhs);

  // We don't check history cutoff here, because it is not an error when the the history cutoff
  // for a later compaction is lower than that for an earlier compaction. This can happen if
  // FLAGS_timestamp_history_retention_interval_sec increases.
  return IsUpdateValidForField(op_id_, rhs.op_id_, update_type) &&
         IsUpdateValidForField(hybrid_time_, rhs.hybrid_time_, update_type);
}

void ConsensusFrontier::UpdateSchemaVersion(
    const Uuid& table_id, SchemaVersion version, rocksdb::UpdateUserValueType type) {
  if (table_id.IsNil()) {
    UpdateField(&primary_schema_version_, std::optional(version), type);
  } else {
    auto it = cotable_schema_versions_.find(table_id);
    if (it == cotable_schema_versions_.end()) {
      cotable_schema_versions_[table_id] = version;
    } else {
      UpdateField(&it->second, version, type);
    }
  }
}

void ConsensusFrontier::AddSchemaVersion(const Uuid& table_id, SchemaVersion version) {
  if (table_id.IsNil()) {
    primary_schema_version_ = version;
  } else {
    cotable_schema_versions_[table_id] = version;
  }
}

void ConsensusFrontier::ResetSchemaVersion() {
  primary_schema_version_.reset();
  cotable_schema_versions_.clear();
}

void ConsensusFrontier::MakeExternalSchemaVersionsAtMost(
    std::unordered_map<Uuid, SchemaVersion, UuidHash>* min_schema_versions) const {
  if (primary_schema_version_) {
    yb::MakeAtMost(Uuid::Nil(), *primary_schema_version_, min_schema_versions);
  }
  for (const auto& p : cotable_schema_versions_) {
    yb::MakeAtMost(p.first, p.second, min_schema_versions);
  }
}

void AddTableSchemaVersion(
    const Uuid& table_id, SchemaVersion schema_version, ConsensusFrontierPB* pb) {
  auto* out = pb->add_table_schema_version();
  if (!table_id.IsNil()) {
    out->set_table_id(table_id.cdata(), table_id.size());
  }
  out->set_schema_version(schema_version);
}

} // namespace docdb
} // namespace yb
