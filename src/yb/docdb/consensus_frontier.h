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

#pragma once

#include <unordered_map>

#include "yb/common/common_fwd.h"
#include "yb/common/entity_ids_types.h"

#include "yb/docdb/docdb.fwd.h"

#include "yb/docdb/docdb_compaction_context.h"
#include "yb/rocksdb/metadata.h"

#include "yb/util/uuid.h"

namespace yb {
namespace docdb {

inline HybridTime NormalizeHistoryCutoff(HybridTime history_cutoff) {
  if (history_cutoff == HybridTime::kMin) {
    return HybridTime::kInvalid;
  }
  return history_cutoff;
}

const size_t kSizeDbOid = sizeof(uint32_t);
const size_t kSizePerDbFilter = kSizeDbOid + HybridTime::SizeOfHybridTimeRepr;

// DocDB implementation of RocksDB UserFrontier. Contains an op id and a hybrid time. The difference
// between this and user boundary values is that here hybrid time is taken from committed Raft log
// entries, whereas user boundary values extract hybrid time from keys in a memtable. This is
// important for transactions, because boundary values would have the commit time of a transaction,
// but e.g. "apply intent" Raft log entries will have a later hybrid time, which would be reflected
// here.
class ConsensusFrontier : public rocksdb::UserFrontier {
 public:
  std::unique_ptr<UserFrontier> Clone() const override {
    return std::make_unique<ConsensusFrontier>(*this);
  }
  ConsensusFrontier() {}
  ConsensusFrontier(const OpId& op_id, HybridTime ht, HistoryCutoff history_cutoff)
      : op_id_(op_id), hybrid_time_(ht) {
    history_cutoff_.primary_cutoff_ht = NormalizeHistoryCutoff(history_cutoff.primary_cutoff_ht);
    history_cutoff_.cotables_cutoff_ht = NormalizeHistoryCutoff(history_cutoff.cotables_cutoff_ht);
  }

  virtual ~ConsensusFrontier();

  bool Equals(const UserFrontier& rhs) const override;
  std::string ToString() const override;
  void ToPB(google::protobuf::Any* pb) const override;
  void Update(const rocksdb::UserFrontier& rhs, rocksdb::UpdateUserValueType type) override;
  bool IsUpdateValid(const rocksdb::UserFrontier& rhs, rocksdb::UpdateUserValueType type) const
      override;
  Status FromPB(const google::protobuf::Any& pb) override;
  void FromOpIdPBDeprecated(const OpIdPB& pb) override;
  Slice FilterAsSlice() override;

  void CotablesFilter(std::vector<std::pair<uint32_t, HybridTime>>* cotables_filter);

  void ResetFilter() override {
    hybrid_time_filter_.clear();
  }

  const OpId& op_id() const { return op_id_; }
  void set_op_id(const OpId& value) { op_id_ = value; }

  template <class PB>
  void set_op_id(const PB& pb) { op_id_ = OpId::FromPB(pb); }

  HybridTime hybrid_time() const { return hybrid_time_; }
  void set_hybrid_time(HybridTime ht) { hybrid_time_ = ht; }

  void SetCoTablesFilter(std::vector<std::pair<uint32_t, HybridTime>> db_oid_to_ht_filter);

  HistoryCutoff history_cutoff() const {
    return history_cutoff_;
  }

  bool history_cutoff_valid() const {
    return history_cutoff_.primary_cutoff_ht.is_valid() ||
           history_cutoff_.cotables_cutoff_ht.is_valid();
  }

  void set_history_cutoff_information(HistoryCutoff history_cutoff) {
    history_cutoff_ = history_cutoff;
  }

  void AppendGlobalFilter(uint64_t value) {
    hybrid_time_filter_.Append(pointer_cast<char*>(&value), sizeof(value));
  }

  void AppendDbOidToCotablesFilter(uint32_t db_oid) {
    hybrid_time_filter_.Append(pointer_cast<char*>(&db_oid), sizeof(db_oid));
  }

  void AppendHybridTimeToCotablesFilter(uint64_t ht) {
    hybrid_time_filter_.Append(pointer_cast<char*>(&ht), sizeof(ht));
  }

  bool HasFilter() const { return !hybrid_time_filter_.empty(); }

  HybridTime GlobalFilter() const;
  void SetGlobalFilter(HybridTime value);

  void UpdateSchemaVersion(
      const Uuid& table_id, SchemaVersion version, rocksdb::UpdateUserValueType type);
  void AddSchemaVersion(const Uuid& table_id, SchemaVersion version);
  void ResetSchemaVersion();

  // Update cotable_id to new_cotable_id in current frontier's cotable_schema_versions_ map.
  // Return true if the map is modified, otherwise, return false.
  bool UpdateCoTableId(const Uuid& cotable_id, const Uuid& new_cotable_id);

  // Merge current frontier with provided map, preferring min values.
  void MakeExternalSchemaVersionsAtMost(
      std::unordered_map<Uuid, SchemaVersion, UuidHash>* min_schema_versions) const;

  HybridTime max_value_level_ttl_expiration_time() const {
    return max_value_level_ttl_expiration_time_;
  }

  void set_max_value_level_ttl_expiration_time(HybridTime ht) {
    max_value_level_ttl_expiration_time_ = ht;
  }

 private:
  OpId op_id_;
  HybridTime hybrid_time_;

  // We use this to keep track of the maximum history cutoff hybrid time used in any compaction, and
  // refuse to perform reads at a hybrid time at which we don't have a valid snapshot anymore. Only
  // the largest frontier of this parameter is being used..
  HistoryCutoff history_cutoff_;

  // Used to track the boundary expiration timestamp for any doc in the file. Tracks value-level
  // TTL expiration (generated at write-time), table-level TTL is calculated at read-time based
  // on hybrid_time_. Only the largest frontier of this parameter is being used.
  HybridTime max_value_level_ttl_expiration_time_;

  std::optional<SchemaVersion> primary_schema_version_;
  std::unordered_map<Uuid, SchemaVersion, UuidHash> cotable_schema_versions_;

  // Serialized filter that is set only for the largest frontier of sst files
  // during restore. There are two types of filter - a global filter
  // that applies to all entries of the sst file and per db filters
  // that apply to keys that have the given db_oid as the prefix. The per db
  // filter is only set for the sys catalog tablet of the master while
  // the global filter can be set for user tablets also. The global filter
  // is just a single Hybrid Time. If an entry has a write hybrid time > this HT
  // then it is ignored during reads. The per db filter has two components - a db oid
  // and a Hybrid Time. If a key has cotable prefix that consists of this db oid then it
  // is ignored if it was written at ht > filter ht. On the other hand if it's db oid is
  // not present in this filter then it is always not ignored. The layout of this byte buffer
  // is as follows:
  /*
  ------------------------------------------------------------------------------------------------
  |  Global HT filter  |    db1 oid    |    db2 oid    |     | db1 HT filter | db2 HT filter |
  | <--- 8 bytes --->  | <- 4 bytes -> | <- 4 bytes -> | ... | <- 8 bytes -> | <- 8 bytes -> | ...
  ------------------------------------------------------------------------------------------------
  */
  ByteBuffer<64> hybrid_time_filter_;
};

typedef rocksdb::UserFrontiersBase<ConsensusFrontier> ConsensusFrontiers;

inline void set_op_id(const OpId& op_id, ConsensusFrontiers* frontiers) {
  frontiers->Smallest().set_op_id(op_id);
  frontiers->Largest().set_op_id(op_id);
}

inline void set_hybrid_time(HybridTime hybrid_time, ConsensusFrontiers* frontiers) {
  frontiers->Smallest().set_hybrid_time(hybrid_time);
  frontiers->Largest().set_hybrid_time(hybrid_time);
}

template <class PB>
void AddTableSchemaVersion(
    const Uuid& table_id, SchemaVersion schema_version, PB* pb) {
  auto* out = pb->add_table_schema_version();
  if (!table_id.IsNil()) {
    out->dup_table_id(table_id.AsSlice());
  }
  out->set_schema_version(schema_version);
}

void AddTableSchemaVersion(
    const Uuid& table_id, SchemaVersion schema_version, ConsensusFrontierPB* pb);

uint64_t ExtractGlobalFilter(Slice filter);
void IterateCotablesFilter(Slice filter, const std::function<void(uint32_t, uint64_t)>& callback);

} // namespace docdb
} // namespace yb
