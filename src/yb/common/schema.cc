// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/common/schema.h"

#include <algorithm>
#include <set>

#include "yb/common/common.pb.h"
#include "yb/common/key_encoder.h"
#include "yb/common/ql_type.h"
#include "yb/common/row.h"

#include "yb/dockv/doc_key.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/join.h"

#include "yb/util/compare_util.h"
#include "yb/util/flags.h"
#include "yb/util/malloc.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"

DEFINE_test_flag(int32, partitioning_version, -1,
                 "When greater than -1, set partitioning_version during table creation.");

namespace yb {

using std::shared_ptr;
using std::unordered_set;
using std::string;
using std::vector;
using dockv::DocKey;
using dockv::KeyEntryValue;

// ------------------------------------------------------------------------------------------------
// ColumnSchema
// ------------------------------------------------------------------------------------------------

ColumnSchema::ColumnSchema(std::string name,
                           DataType type,
                           ColumnKind kind,
                           Nullable is_nullable,
                           bool is_static,
                           bool is_counter,
                           int32_t order,
                           int32_t pg_type_oid,
                           bool marked_for_deletion,
                           const QLValuePB& missing_value)
    : ColumnSchema(std::move(name), QLType::Create(type), kind, is_nullable, is_static, is_counter,
                   order, pg_type_oid, marked_for_deletion, missing_value) {
}

const TypeInfo* ColumnSchema::type_info() const {
  return type_->type_info();
}

bool ColumnSchema::is_collection() const {
  return type_info()->is_collection();
}

bool ColumnSchema::CompTypeInfo(const ColumnSchema &a, const ColumnSchema &b) {
  return a.type_info()->type == b.type_info()->type;
}

int ColumnSchema::Compare(const void *lhs, const void *rhs) const {
  return type_info()->Compare(lhs, rhs);
}

// Stringify the given cell. This just stringifies the cell contents,
// and doesn't include the column name or type.
std::string ColumnSchema::Stringify(const void *cell) const {
  std::string ret;
  type_info()->AppendDebugStringForValue(cell, &ret);
  return ret;
}

void ColumnSchema::DoDebugCellAppend(const void* cell, std::string* ret) const {
  ret->append(type_info()->name);
  ret->append(" ");
  ret->append(name_);
  ret->append("=");
  if (is_nullable_ && cell == nullptr) {
    ret->append("NULL");
  } else {
    type_info()->AppendDebugStringForValue(cell, ret);
  }
}

// TODO: include attributes_.ToString() -- need to fix unit tests
// first
string ColumnSchema::ToString() const {
  return strings::Substitute("$0[$1]",
                             name_,
                             TypeToString());
}

string ColumnSchema::TypeToString() const {
  return Format("$0 $1 $2",
                type_info()->name,
                is_nullable_ ? "NULLABLE" : "NOT NULL",
                kind_);
}

size_t ColumnSchema::memory_footprint_excluding_this() const {
  // Rough approximation.
  return name_.capacity();
}

size_t ColumnSchema::memory_footprint_including_this() const {
  return malloc_usable_size(this) + memory_footprint_excluding_this();
}

bool ColumnSchema::TEST_Equals(const ColumnSchema& lhs, const ColumnSchema& rhs) {
  return lhs.Equals(rhs) &&
         YB_STRUCT_EQUALS(is_nullable_,
                          kind_,
                          is_static_,
                          is_counter_,
                          order_,
                          pg_type_oid_,
                          marked_for_deletion_,
                          missing_value_);
}

SortingType ColumnSchema::sorting_type() const {
  switch (kind_) {
    case ColumnKind::HASH: [[fallthrough]];
    case ColumnKind::VALUE:
      return SortingType::kNotSpecified;
    case ColumnKind::RANGE_ASC_NULL_FIRST:
      return SortingType::kAscending;
    case ColumnKind::RANGE_DESC_NULL_FIRST:
      return SortingType::kDescending;
    case ColumnKind::RANGE_ASC_NULL_LAST:
      return SortingType::kAscendingNullsLast;
    case ColumnKind::RANGE_DESC_NULL_LAST:
      return SortingType::kDescendingNullsLast;
  }
  FATAL_INVALID_ENUM_VALUE(ColumnKind, kind_);
}

// ------------------------------------------------------------------------------------------------
// TableProperties
// ------------------------------------------------------------------------------------------------

void TableProperties::ToTablePropertiesPB(TablePropertiesPB *pb) const {
  if (HasDefaultTimeToLive()) {
    pb->set_default_time_to_live(default_time_to_live_);
  }
  pb->set_contain_counters(contain_counters_);
  pb->set_is_transactional(is_transactional_);
  pb->set_consistency_level(consistency_level_);
  pb->set_use_mangled_column_name(use_mangled_column_name_);
  if (HasNumTablets()) {
    pb->set_num_tablets(num_tablets_);
  }
  pb->set_is_ysql_catalog_table(is_ysql_catalog_table_);
  pb->set_retain_delete_markers(retain_delete_markers_);
  pb->set_partitioning_version(partitioning_version_);
  if (HasReplicaIdentity()) {
    pb->set_ysql_replica_identity(*ysql_replica_identity_);
  }
}

TableProperties TableProperties::FromTablePropertiesPB(const TablePropertiesPB& pb) {
  TableProperties table_properties;
  if (pb.has_default_time_to_live()) {
    table_properties.SetDefaultTimeToLive(pb.default_time_to_live());
  }
  if (pb.has_contain_counters()) {
    table_properties.SetContainCounters(pb.contain_counters());
  }
  if (pb.has_is_transactional()) {
    table_properties.SetTransactional(pb.is_transactional());
  }
  if (pb.has_consistency_level()) {
    table_properties.SetConsistencyLevel(pb.consistency_level());
  }
  if (pb.has_use_mangled_column_name()) {
    table_properties.SetUseMangledColumnName(pb.use_mangled_column_name());
  }
  if (pb.has_num_tablets()) {
    table_properties.SetNumTablets(pb.num_tablets());
  }
  if (pb.has_is_ysql_catalog_table()) {
    table_properties.set_is_ysql_catalog_table(pb.is_ysql_catalog_table());
  }
  if (pb.has_retain_delete_markers()) {
    table_properties.SetRetainDeleteMarkers(pb.retain_delete_markers());
  }
  if (pb.has_ysql_replica_identity()) {
    table_properties.SetReplicaIdentity(pb.ysql_replica_identity());
  }
  table_properties.set_partitioning_version(
      pb.has_partitioning_version() ? pb.partitioning_version() : 0);
  return table_properties;
}

void TableProperties::AlterFromTablePropertiesPB(const TablePropertiesPB& pb) {
  if (pb.has_default_time_to_live()) {
    SetDefaultTimeToLive(pb.default_time_to_live());
  }
  if (pb.has_is_transactional()) {
    SetTransactional(pb.is_transactional());
  }
  if (pb.has_consistency_level()) {
    SetConsistencyLevel(pb.consistency_level());
  }
  if (pb.has_use_mangled_column_name()) {
    SetUseMangledColumnName(pb.use_mangled_column_name());
  }
  if (pb.has_num_tablets()) {
    SetNumTablets(pb.num_tablets());
  }
  if (pb.has_is_ysql_catalog_table()) {
    set_is_ysql_catalog_table(pb.is_ysql_catalog_table());
  }
  if (pb.has_retain_delete_markers()) {
    SetRetainDeleteMarkers(pb.retain_delete_markers());
  }
  if (pb.has_ysql_replica_identity()) {
    SetReplicaIdentity(pb.ysql_replica_identity());
  }
  set_partitioning_version(pb.has_partitioning_version() ? pb.partitioning_version() : 0);
}

void TableProperties::Reset() {
  default_time_to_live_ = kNoDefaultTtl;
  contain_counters_ = false;
  is_transactional_ = false;
  consistency_level_ = YBConsistencyLevel::STRONG;
  use_mangled_column_name_ = false;
  num_tablets_ = 0;
  is_ysql_catalog_table_ = false;
  retain_delete_markers_ = false;
  partitioning_version_ =
      PREDICT_TRUE(FLAGS_TEST_partitioning_version < 0) ? kCurrentPartitioningVersion
                                                        : FLAGS_TEST_partitioning_version;
  ysql_replica_identity_ = std::nullopt;
}

string TableProperties::ToString() const {
  std::string result("{ ");
  if (HasDefaultTimeToLive()) {
    result += Format("default_time_to_live: $0 ", default_time_to_live_);
  }
  result += Format("contain_counters: $0 is_transactional: $1 ",
                   contain_counters_, is_transactional_);
  result + Format(
               "consistency_level: $0 is_ysql_catalog_table: $1 partitioning_version: $2 ",
               consistency_level_, is_ysql_catalog_table_, partitioning_version_);
  if (HasReplicaIdentity()) {
    result + Format("replica_identity: $0 }", *ysql_replica_identity_);
  }
  return result;
}

// ------------------------------------------------------------------------------------------------
// Schema
// ------------------------------------------------------------------------------------------------

Schema::Schema(const Schema& other)
  : // TODO: C++11 provides a single-arg constructor
    name_to_index_(10,
                   NameToIndexMap::hasher(),
                   NameToIndexMap::key_equal(),
                   NameToIndexMapAllocator(&name_to_index_bytes_)) {
  CopyFrom(other);
}

Schema::Schema(const vector<ColumnSchema>& cols,
               const TableProperties& table_properties,
               const Uuid& cotable_id,
               const ColocationId colocation_id,
               const PgSchemaName pgschema_name)
  : // TODO: C++11 provides a single-arg constructor
    name_to_index_(10,
                   NameToIndexMap::hasher(),
                   NameToIndexMap::key_equal(),
                   NameToIndexMapAllocator(&name_to_index_bytes_)) {
  CHECK_OK(Reset(cols, table_properties, cotable_id, colocation_id, pgschema_name));
}

Schema::Schema(const vector<ColumnSchema>& cols,
               const vector<ColumnId>& ids,
               const TableProperties& table_properties,
               const Uuid& cotable_id,
               const ColocationId colocation_id,
               const PgSchemaName pgschema_name)
  : // TODO: C++11 provides a single-arg constructor
    name_to_index_(10,
                   NameToIndexMap::hasher(),
                   NameToIndexMap::key_equal(),
                   NameToIndexMapAllocator(&name_to_index_bytes_)) {
  CHECK_OK(Reset(cols, ids, table_properties, cotable_id, colocation_id, pgschema_name));
}

Schema& Schema::operator=(const Schema& other) {
  if (&other != this) {
    CopyFrom(other);
  }
  return *this;
}

void Schema::CopyFrom(const Schema& other) {
  num_key_columns_ = other.num_key_columns_;
  num_hash_key_columns_ = other.num_hash_key_columns_;
  max_col_id_ = other.max_col_id_;
  cols_ = other.cols_;
  col_ids_ = other.col_ids_;
  col_offsets_ = other.col_offsets_;
  id_to_index_ = other.id_to_index_;

  // We can't simply copy name_to_index_ since the GStringPiece keys
  // reference the other Schema's ColumnSchema objects.
  name_to_index_.clear();
  int i = 0;
  for (const ColumnSchema &col : cols_) {
    // The map uses the 'name' string from within the ColumnSchema object.
    name_to_index_[col.name()] = i++;
  }

  has_nullables_ = other.has_nullables_;
  has_statics_ = other.has_statics_;
  table_properties_ = other.table_properties_;
  cotable_id_ = other.cotable_id_;
  colocation_id_ = other.colocation_id_;
  pgschema_name_ = other.pgschema_name_;

  // Schema cannot have both cotable ID and colocation ID.
  DCHECK(cotable_id_.IsNil() || colocation_id_ == kColocationIdNotSet);
}

void Schema::ResetColumnIds(const vector<ColumnId>& ids) {
  // Initialize IDs mapping.
  col_ids_ = ids;
  id_to_index_.clear();
  max_col_id_ = 0;
  for (size_t i = 0; i < ids.size(); ++i) {
    if (ids[i] > max_col_id_) {
      max_col_id_ = ids[i];
    }
    id_to_index_.set(ids[i], narrow_cast<int>(i));
  }
}

Status Schema::Reset(const vector<ColumnSchema>& cols,
                     const TableProperties& table_properties,
                     const Uuid& cotable_id,
                     const ColocationId colocation_id,
                     const PgSchemaName pgschema_name) {
  return Reset(cols, {}, table_properties, cotable_id, colocation_id, pgschema_name);
}

Status Schema::Reset(const vector<ColumnSchema>& cols,
                     const vector<ColumnId>& ids,
                     const TableProperties& table_properties,
                     const Uuid& cotable_id,
                     const ColocationId colocation_id,
                     const PgSchemaName pgschema_name) {
  cols_ = cols;
  num_key_columns_ = 0;
  num_hash_key_columns_ = 0;
  table_properties_ = table_properties;
  cotable_id_ = cotable_id;
  colocation_id_ = colocation_id;
  pgschema_name_ = pgschema_name;

  // Determine whether any column is nullable or static, and count number of hash columns.
  has_nullables_ = false;
  has_statics_ = false;
  for (const ColumnSchema& col : cols_) {
    if (col.is_key()) {
      ++num_key_columns_;
      if (col.is_hash_key()) {
        ++num_hash_key_columns_;
      }
    }
    if (col.is_nullable()) {
      has_nullables_ = true;
    }
    if (col.is_static()) {
      has_statics_ = true;
    }
  }

  if (PREDICT_FALSE(!ids.empty() && ids.size() != cols_.size())) {
    return STATUS(InvalidArgument, "Bad schema",
      "The number of ids does not match with the number of columns");
  }

  if (PREDICT_FALSE(!cotable_id.IsNil() && colocation_id != kColocationIdNotSet)) {
    return STATUS(InvalidArgument,
                  "Bad schema", "Cannot have both cotable ID and colocation ID");
  }

  // Calculate the offset of each column in the row format.
  col_offsets_.reserve(cols_.size() + 1);  // Include space for total byte size at the end.
  size_t off = 0;
  size_t idx = 0;
  name_to_index_.clear();
  for (const ColumnSchema &col : cols_) {
    // The map uses the 'name' string from within the ColumnSchema object.
    if (!InsertIfNotPresent(&name_to_index_, col.name(), idx++)) {
      return STATUS(InvalidArgument, "Duplicate column name", col.name());
    }

    col_offsets_.push_back(off);
    off += col.type_info()->size;
  }

  // Add an extra element on the end for the total
  // byte size
  col_offsets_.push_back(off);

  // Initialize IDs mapping
  ResetColumnIds(ids);

  return Status::OK();
}

Status Schema::TEST_CreateProjectionByNames(
    const std::vector<GStringPiece>& col_names, Schema* out) const {
  vector<ColumnId> ids;
  vector<ColumnSchema> cols;
  for (const GStringPiece& name : col_names) {
    auto idx = find_column(name);
    if (idx == kColumnNotFound) {
      return STATUS(NotFound, "Column not found", name);
    }
    if (has_column_ids()) {
      ids.push_back(column_id(idx));
    }
    cols.push_back(column(idx));
  }
  return out->Reset(cols, ids, TableProperties(), cotable_id_, colocation_id_, pgschema_name_);
}

Status Schema::CreateProjectionByIdsIgnoreMissing(const std::vector<ColumnId>& col_ids,
                                                  Schema* out) const {
  vector<ColumnSchema> cols;
  vector<ColumnId> filtered_col_ids;
  for (ColumnId id : col_ids) {
    int idx = find_column_by_id(id);
    if (idx == -1) {
      continue;
    }
    cols.push_back(column(idx));
    filtered_col_ids.push_back(id);
  }
  return out->Reset(
      cols, filtered_col_ids, TableProperties(), cotable_id_, colocation_id_, pgschema_name_);
}

Schema Schema::CreateKeyProjection() const {
  std::vector<ColumnSchema> key_cols(cols_.begin(), cols_.begin() + num_key_columns_);
  std::vector<ColumnId> col_ids;
  if (!col_ids_.empty()) {
    col_ids.assign(col_ids_.begin(), col_ids_.begin() + num_key_columns_);
  }

  return Schema(key_cols, col_ids);
}

namespace {

vector<ColumnId> DefaultColumnIds(ColumnIdRep num_columns) {
  vector<ColumnId> ids;
  for (ColumnIdRep i = 0; i < num_columns; ++i) {
    ids.push_back(ColumnId(kFirstColumnId + i));
  }
  return ids;
}

}  // namespace

void Schema::InitColumnIdsByDefault() {
  CHECK(!has_column_ids());
  ResetColumnIds(DefaultColumnIds(narrow_cast<ColumnIdRep>(cols_.size())));
}

Status Schema::VerifyProjectionCompatibility(const Schema& projection) const {
  DCHECK(has_column_ids()) << "The server schema must have IDs";

  if (projection.has_column_ids()) {
    return STATUS(InvalidArgument, "User requests should not have Column IDs");
  }

  vector<string> missing_columns;
  for (const ColumnSchema& pcol : projection.columns()) {
    auto index = find_column(pcol.name());
    if (index == kColumnNotFound) {
      missing_columns.push_back(pcol.name());
    } else if (!pcol.EqualsType(cols_[index])) {
      // TODO: We don't support query with type adaptors yet
      return STATUS(InvalidArgument, "The column '" + pcol.name() + "' must have type " +
                                     cols_[index].TypeToString() + " found " + pcol.TypeToString());
    }
  }

  if (!missing_columns.empty()) {
    return STATUS(InvalidArgument, "Some columns are not present in the current schema",
                                   JoinStrings(missing_columns, ", "));
  }
  return Status::OK();
}

std::string Schema::ToString() const {
  vector<string> col_strs;
  if (has_column_ids()) {
    for (size_t i = 0; i < cols_.size(); ++i) {
      col_strs.push_back(Format("$0:$1", col_ids_[i], cols_[i].ToString()));
    }
  } else {
    for (const ColumnSchema &col : cols_) {
      col_strs.push_back(col.ToString());
    }
  }

  TablePropertiesPB tablet_properties_pb;
  table_properties_.ToTablePropertiesPB(&tablet_properties_pb);

  return StrCat("Schema [\n\t",
                JoinStrings(col_strs, ",\n\t"),
                "\n]\nproperties: ",
                tablet_properties_pb.ShortDebugString(),
                cotable_id_.IsNil() ? "" : ("\ncotable_id: " + cotable_id_.ToString()),
                colocation_id_ == kColocationIdNotSet
                    ? "" : ("\ncolocation_id: " + std::to_string(colocation_id_)));
}

size_t Schema::memory_footprint_excluding_this() const {
  size_t size = 0;
  for (const ColumnSchema& col : cols_) {
    size += col.memory_footprint_excluding_this();
  }

  if (cols_.capacity() > 0) {
    size += malloc_usable_size(cols_.data());
  }
  if (col_ids_.capacity() > 0) {
    size += malloc_usable_size(col_ids_.data());
  }
  if (col_offsets_.capacity() > 0) {
    size += malloc_usable_size(col_offsets_.data());
  }
  size += name_to_index_bytes_;
  size += id_to_index_.memory_footprint_excluding_this();

  return size;
}

size_t Schema::memory_footprint_including_this() const {
  return malloc_usable_size(this) + memory_footprint_excluding_this();
}

Result<ssize_t> Schema::ColumnIndexByName(GStringPiece col_name) const {
  auto index = find_column(col_name);
  if (index == kColumnNotFound) {
    return STATUS_FORMAT(Corruption, "$0 not found in schema $1", col_name, name_to_index_);
  }
  return index;
}

Result<ColumnId> Schema::ColumnIdByName(const std::string& column_name) const {
  auto column_index = find_column(column_name);
  if (column_index == kColumnNotFound) {
    return STATUS_FORMAT(NotFound, "Couldn't find column $0 in the schema", column_name);
  }
  return ColumnId(column_id(column_index));
}

ColumnId Schema::first_column_id() {
  return kFirstColumnId;
}

Result<const ColumnSchema&> Schema::column_by_id(ColumnId id) const {
  int idx = find_column_by_id(id);
  if (idx < 0) {
    return STATUS_FORMAT(InvalidArgument, "Column id $0 not found", id.ToString());
  }
  return cols_[idx];
}

void Schema::UpdateMissingValuesFrom(
    const google::protobuf::RepeatedPtrField<ColumnSchemaPB>& columns) {
  for (int i = 0; i < columns.size(); ++i) {
    if (columns[i].has_missing_value()) {
      cols_[i].set_missing_value(columns[i].missing_value());
    }
  }
}

Result<const QLValuePB&> Schema::GetMissingValueByColumnId(ColumnId id) const {
  const auto& column_schema = VERIFY_RESULT_REF(column_by_id(id));
  return column_schema.missing_value();
}

bool Schema::TEST_Equals(const Schema& lhs, const Schema& rhs) {
  return lhs.Equals(rhs) &&
         YB_STRUCT_EQUALS(num_hash_key_columns_,
                          max_col_id_,
                          col_ids_,
                          col_offsets_,
                          name_to_index_,
                          name_to_index_,
                          id_to_index_,
                          has_nullables_,
                          has_statics_,
                          cotable_id_,
                          colocation_id_,
                          pgschema_name_);
}

// ============================================================================
//  Schema Builder
// ============================================================================
void SchemaBuilder::Reset() {
  cols_.clear();
  col_ids_.clear();
  col_names_.clear();
  num_key_columns_ = 0;
  next_id_ = kFirstColumnId;
  table_properties_.Reset();
  colocation_id_ = kColocationIdNotSet;
  pgschema_name_ = "";
  cotable_id_ = Uuid::Nil();
}

void SchemaBuilder::Reset(const Schema& schema) {
  cols_ = schema.cols_;
  col_ids_ = schema.col_ids_;
  num_key_columns_ = schema.num_key_columns_;
  for (const auto& column : cols_) {
    col_names_.insert(column.name());
  }

  if (col_ids_.empty()) {
    for (ColumnIdRep i = 0; i < narrow_cast<ColumnIdRep>(cols_.size()); ++i) {
      col_ids_.push_back(ColumnId(kFirstColumnId + i));
    }
  }
  if (col_ids_.empty()) {
    next_id_ = kFirstColumnId;
  } else {
    next_id_ = *std::max_element(col_ids_.begin(), col_ids_.end()) + 1;
  }
  table_properties_ = schema.table_properties_;
  pgschema_name_ = schema.pgschema_name_;
  cotable_id_ = schema.cotable_id_;
  colocation_id_ = schema.colocation_id_;
}

Status SchemaBuilder::AddKeyColumn(const string& name, const shared_ptr<QLType>& type) {
  return AddColumn(ColumnSchema(name, type, ColumnKind::RANGE_ASC_NULL_FIRST));
}

Status SchemaBuilder::AddKeyColumn(const string& name, DataType type) {
  return AddColumn(ColumnSchema(name, QLType::Create(type), ColumnKind::RANGE_ASC_NULL_FIRST));
}

Status SchemaBuilder::AddHashKeyColumn(const string& name, const shared_ptr<QLType>& type) {
  return AddColumn(ColumnSchema(name, type, ColumnKind::HASH));
}

Status SchemaBuilder::AddHashKeyColumn(const string& name, DataType type) {
  return AddColumn(ColumnSchema(name, QLType::Create(type), ColumnKind::HASH));
}

Status SchemaBuilder::AddColumn(const std::string& name, DataType type) {
  return AddColumn(name, QLType::Create(type));
}

Status SchemaBuilder::AddColumn(const std::string& name,
                                DataType type,
                                Nullable is_nullable,
                                bool is_static,
                                bool is_counter,
                                int32_t order) {
  return AddColumn(name, QLType::Create(type), is_nullable, is_static, is_counter, order);
}

Status SchemaBuilder::AddColumn(const string& name,
                                const std::shared_ptr<QLType>& type,
                                Nullable is_nullable,
                                bool is_static,
                                bool is_counter,
                                int32_t order) {
  return AddColumn(ColumnSchema(
      name, type, ColumnKind::VALUE, is_nullable, is_static, is_counter, order));
}


Status SchemaBuilder::AddNullableColumn(const std::string& name, DataType type) {
  return AddNullableColumn(name, QLType::Create(type));
}

Status SchemaBuilder::RemoveColumn(const string& name) {
  unordered_set<string>::const_iterator it_names;
  if ((it_names = col_names_.find(name)) == col_names_.end()) {
    return STATUS(NotFound, "The specified column does not exist", name);
  }

  col_names_.erase(it_names);
  for (size_t i = 0; i < cols_.size(); ++i) {
    if (name == cols_[i].name()) {
      cols_.erase(cols_.begin() + i);
      col_ids_.erase(col_ids_.begin() + i);
      if (i < num_key_columns_) {
        num_key_columns_--;
      }
      return Status::OK();
    }
  }

  LOG(FATAL) << "Should not reach here";
  return STATUS(Corruption, "Unable to remove existing column");
}

Status SchemaBuilder::RenameColumn(const string& old_name, const string& new_name) {
  unordered_set<string>::const_iterator it_names;

  // check if 'new_name' is already in use
  if ((it_names = col_names_.find(new_name)) != col_names_.end()) {
    return STATUS(AlreadyPresent, "The column already exists", new_name);
  }

  // check if the 'old_name' column exists
  if ((it_names = col_names_.find(old_name)) == col_names_.end()) {
    return STATUS(NotFound, "The specified column does not exist", old_name);
  }

  col_names_.erase(it_names);   // TODO: Should this one stay and marked as alias?
  col_names_.insert(new_name);

  for (ColumnSchema& col_schema : cols_) {
    if (old_name == col_schema.name()) {
      col_schema.set_name(new_name);
      return Status::OK();
    }
  }

  LOG(FATAL) << "Should not reach here";
  return STATUS(IllegalState, "Unable to rename existing column");
}

Status SchemaBuilder::SetColumnPGType(const string& name, const uint32_t pg_type_oid) {
  for (ColumnSchema& col_schema : cols_) {
    if (name == col_schema.name()) {
      col_schema.set_pg_type_oid(pg_type_oid);
      return Status::OK();
    }
  }
  return STATUS(NotFound, "The specified column does not exist", name);
}

Status SchemaBuilder::MarkColumnForDeletion(const string& name) {
  for (size_t i = 0; i < cols_.size(); ++i) {
    ColumnSchema& col = cols_[i];
    if (name == col.name()) {
      SCHECK(i >= num_key_columns_, InvalidArgument, "Cannot mark a key column for deletion", name);
      col.set_marked_for_deletion(true);
      return Status::OK();
    }
  }
  return STATUS(NotFound, "The specified column does not exist", name);
}

Status SchemaBuilder::SetColumnPGTypmod(const std::string& name, const uint32_t pg_typmod) {
  for (ColumnSchema& col_schema : cols_) {
    if (name == col_schema.name()) {
      col_schema.set_pg_typmod(pg_typmod);
      return Status::OK();
    }
  }
  return STATUS(NotFound, "The specified column does not exist", name);
}

Status SchemaBuilder::AddColumn(const ColumnSchema& column) {
  for (const ColumnSchema& col : cols_) {
    if (column.name() == col.name()) {
      if (col.marked_for_deletion()) {
        return STATUS_FORMAT(AlreadyPresent, "The column $0 is still in process of deletion",
            column.name());
      }
      return STATUS(AlreadyPresent, "The column already exists", column.name());
    }
  }

  col_names_.insert(column.name());
  if (column.is_key()) {
    cols_.insert(cols_.begin() + num_key_columns_, column);
    col_ids_.insert(col_ids_.begin() + num_key_columns_, next_id_);
    num_key_columns_++;
  } else {
    cols_.push_back(column);
    col_ids_.push_back(next_id_);
  }

  next_id_ = ColumnId(next_id_ + 1);
  return Status::OK();
}

Status SchemaBuilder::AlterProperties(const TablePropertiesPB& pb) {
  table_properties_.AlterFromTablePropertiesPB(pb);
  return Status::OK();
}


Status DeletedColumn::FromPB(const DeletedColumnPB& col, DeletedColumn* ret) {
  ret->id = col.column_id();
  ret->ht = HybridTime(col.deleted_hybrid_time());
  return Status::OK();
}

void DeletedColumn::CopyToPB(DeletedColumnPB* pb) const {
  pb->set_column_id(id);
  pb->set_deleted_hybrid_time(ht.ToUint64());
}

ColumnKind SortingTypeToColumnKind(SortingType sorting_type) {
  switch (sorting_type) {
    case SortingType::kNotSpecified: [[fallthrough]];
    case SortingType::kAscending:
      return ColumnKind::RANGE_ASC_NULL_FIRST;
    case SortingType::kDescending:
      return ColumnKind::RANGE_DESC_NULL_FIRST;
    case SortingType::kAscendingNullsLast:
      return ColumnKind::RANGE_ASC_NULL_LAST;
    case SortingType::kDescendingNullsLast:
      return ColumnKind::RANGE_DESC_NULL_LAST;
  }
  FATAL_INVALID_ENUM_VALUE(SortingType, sorting_type);
}

} // namespace yb
