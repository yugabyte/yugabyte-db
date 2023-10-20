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

#include "yb/docdb/doc_read_context.h"

#include "yb/common/ql_type.h"

#include "yb/dockv/value_type.h"

#include "yb/util/logging.h"

namespace yb::docdb {

DocReadContext::DocReadContext(
    const std::string& log_prefix, TableType table_type, Index is_index)
    : schema_packing_storage(table_type), is_index_(is_index),
      log_prefix_(log_prefix) {
  UpdateKeyPrefix();
}

DocReadContext::DocReadContext(
    const std::string& log_prefix, TableType table_type, Index is_index, const Schema& schema,
    SchemaVersion schema_version)
    : schema_packing_storage(table_type), is_index_(is_index), schema_(schema),
      log_prefix_(log_prefix) {
  schema_packing_storage.AddSchema(schema_version, schema_);
  UpdateKeyPrefix();
  LOG_IF_WITH_PREFIX(INFO, schema_version != 0)
      << "DocReadContext, from schema, version: " << schema_version;
}

DocReadContext::DocReadContext(
    const DocReadContext& rhs, const Schema& schema, SchemaVersion schema_version)
    : schema_packing_storage(rhs.schema_packing_storage), is_index_(rhs.is_index_), schema_(schema),
      log_prefix_(rhs.log_prefix_) {
  schema_packing_storage.AddSchema(schema_version, schema_);
  UpdateKeyPrefix();
  LOG_WITH_PREFIX(INFO)
      << "DocReadContext, copy and add: " << schema_packing_storage.VersionsToString()
      << ", added: " << schema_version;
}

DocReadContext::DocReadContext(const DocReadContext& rhs, SchemaVersion min_schema_version)
    : schema_packing_storage(rhs.schema_packing_storage, min_schema_version),
      is_index_(rhs.is_index_), schema_(rhs.schema_), log_prefix_(rhs.log_prefix_) {
  UpdateKeyPrefix();
  LOG_WITH_PREFIX(INFO)
      << "DocReadContext, copy and filter: " << rhs.schema_packing_storage.VersionsToString()
      << " => " << schema_packing_storage.VersionsToString() << ", min_schema_version: "
      << min_schema_version;
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
             !is_index_ && schema_.columns()[0].kind() == ColumnKind::RANGE_ASC_NULL_FIRST) {
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

} // namespace yb::docdb
