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

#include "yb/common/schema.h"
#include "yb/common/schema_pbutil.h"
#include "yb/common/wire_protocol.h"

#include "yb/dockv/schema_packing.h"

namespace yb::docdb {

YB_STRONGLY_TYPED_BOOL(Index);

struct DocReadContext {
  DocReadContext(
      const std::string& log_prefix, TableType table_type, Index is_index);

  DocReadContext(
      const std::string& log_prefix, TableType table_type, Index is_index, const Schema& schema,
      SchemaVersion schema_version);

  DocReadContext(const DocReadContext& rhs, const Schema& schema, SchemaVersion schema_version);

  DocReadContext(const DocReadContext& rhs, const Schema& schema);

  DocReadContext(const DocReadContext& rhs, SchemaVersion min_schema_version);

  template <class PB>
  Status LoadFromPB(const PB& pb) {
    RETURN_NOT_OK(SchemaFromPB(pb.schema(), &schema_));
    RETURN_NOT_OK(schema_packing_storage.LoadFromPB(pb.old_schema_packings()));
    schema_packing_storage.AddSchema(pb.schema_version(), schema_);
    UpdateKeyPrefix();
    LogAfterLoad();
    return Status::OK();
  }

  template <class PB>
  Status MergeWithRestored(const PB& pb, dockv::OverwriteSchemaPacking overwrite) {
    RETURN_NOT_OK(schema_packing_storage.MergeWithRestored(
        pb.schema_version(), pb.schema(), pb.old_schema_packings(), overwrite));
    LogAfterMerge(overwrite);
    UpdateKeyPrefix();
    return Status::OK();
  }

  template <class PB>
  void ToPB(SchemaVersion schema_version, PB* out) const {
    DCHECK(schema_.has_column_ids());
    SchemaToPB(schema_, out->mutable_schema());
    schema_packing_storage.ToPB(schema_version, out->mutable_old_schema_packings());
  }

  const Schema& schema() const {
    return schema_;
  }

  Schema* mutable_schema() {
    return &schema_;
  }

  void SetCotableId(const Uuid& cotable_id);

  // The number of bytes before actual key values for all encoded keys in this table.
  size_t key_prefix_encoded_len() const {
    return key_prefix_encoded_len_;
  }

  Slice shared_key_prefix() const {
    return Slice(shared_key_prefix_buffer_.data(), shared_key_prefix_len_);
  }

  Slice upperbound() const {
    return Slice(upperbound_buffer_.data(), upperbound_len_);
  }

  Slice table_key_prefix() const {
    return Slice(shared_key_prefix_buffer_.data(), table_key_prefix_len_);
  }

  void TEST_SetDefaultTimeToLive(uint64_t ttl_msec) {
    schema_.SetDefaultTimeToLive(ttl_msec);
  }

  // Should account for every field in DocReadContext.
  static bool TEST_Equals(const DocReadContext& lhs, const DocReadContext& rhs) {
    return Schema::TEST_Equals(lhs.schema_, rhs.schema_) &&
        lhs.schema_packing_storage == rhs.schema_packing_storage;
  }

  static DocReadContext TEST_Create(const Schema& schema) {
    return DocReadContext(
        "TEST: ", TableType::YQL_TABLE_TYPE, Index::kFalse, schema, 0);
  }

  dockv::SchemaPackingStorage schema_packing_storage;

 private:
  void LogAfterLoad();
  void LogAfterMerge(dockv::OverwriteSchemaPacking overwrite);
  void UpdateKeyPrefix();

  const std::string& LogPrefix() const {
    return log_prefix_;
  }

  Index is_index_;
  Schema schema_;

  // The data about key prefix shared by all entries of this table.
  // shared_key_prefix_* fields store prefix bytes common to all keys in this table.
  // I.e. if table has cotable id or colocation id, then it will be placed in shared_key_prefix_*.
  // Also in case of non empty hash part, it will contain kUInt16Hash byte.
  // When hash part is empty and first byte of encoded range column is the same for all entries
  // in the table, then it will also be present here.
  size_t shared_key_prefix_len_ = 0;
  std::array<uint8_t, 0x20> shared_key_prefix_buffer_;

  // The data about upperbound for this table. I.e. we know that all entries from this table
  // are before the upperbound. And all entries from next table are after this upperbound.
  size_t upperbound_len_ = 0;
  std::array<uint8_t, 0x20> upperbound_buffer_;

  // This field contains number of bytes in encoded key before column values.
  // I.e. it is sum of sizes of cotable id, colocation id, hash code.
  // It is very close to shared_key_prefix_len_ with exception that shared_key_prefix_len_
  // has only one byte for hash code, i.e. shared key entry value type. But not the value of
  // hash code itself.
  // While key_prefix_encoded_len_ will have 3 bytes for it, i.e. full encoded hash code.
  size_t key_prefix_encoded_len_ = 0;

  // Includes cotable_id and colocation_id.
  size_t table_key_prefix_len_ = 0;

  std::string log_prefix_;
};

} // namespace yb::docdb
