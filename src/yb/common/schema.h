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
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <boost/optional/optional.hpp>

#include "yb/util/logging.h"

#include "yb/common/column_id.h"
#include "yb/common/common_types.pb.h"
#include "yb/common/common_fwd.h"
#include "yb/common/constants.h"
#include "yb/common/entity_ids_types.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/id_mapping.h"
#include "yb/common/types.h"

#include "yb/gutil/stl_util.h"

#include "yb/util/memory/arena_fwd.h"
#include "yb/util/status.h"
#include "yb/util/uuid.h"

// Check that two schemas are equal, yielding a useful error message in the case that
// they are not.
#define DCHECK_SCHEMA_EQ(s1, s2) \
  do { \
    DCHECK((s1).Equals((s2))) << "Schema " << (s1).ToString() \
                              << " does not match " << (s2).ToString(); \
  } while (0);

#define DCHECK_KEY_PROJECTION_SCHEMA_EQ(s1, s2) \
  do { \
    DCHECK((s1).KeyEquals((s2))) << "Key-Projection Schema " \
                                 << (s1).ToString() << " does not match " \
                                 << (s2).ToString(); \
  } while (0);

namespace yb {

class DeletedColumnPB;

static const int kNoDefaultTtl = -1;
static const int kYbHashCodeColId = std::numeric_limits<int16_t>::max() - 1;

// Struct for storing information about deleted columns for cleanup.
struct DeletedColumn {
  ColumnId id;
  HybridTime ht;

  DeletedColumn() { }

  DeletedColumn(ColumnId id_, HybridTime ht_) : id(id_), ht(ht_) {}

  static Status FromPB(const DeletedColumnPB& col, DeletedColumn* ret);
  void CopyToPB(DeletedColumnPB* pb) const;

  friend bool operator==(const DeletedColumn&, const DeletedColumn&) = default;
};

// The schema for a given column.
//
// Holds the data type as well as information about nullability & column name.
// In the future, it may hold information about annotations, etc.
class ColumnSchema {
 public:
  // Component comparators for combining in custom comparators.
  static bool CompName(const ColumnSchema &a, const ColumnSchema &b) {
    return a.name_ == b.name_;
  }

  static bool CompNullable(const ColumnSchema &a, const ColumnSchema &b) {
    return a.is_nullable_ == b.is_nullable_;
  }

  static bool CompHashKey(const ColumnSchema &a, const ColumnSchema &b) {
    return a.is_hash_key_ == b.is_hash_key_;
  }

  static bool CompSortingType(const ColumnSchema &a, const ColumnSchema &b) {
    return a.sorting_type_ == b.sorting_type_;
  }

  static bool CompTypeInfo(const ColumnSchema &a, const ColumnSchema &b);

  static bool CompOrder(const ColumnSchema &a, const ColumnSchema &b) {
    return a.order_ == b.order_;
  }

  // Combined comparators.
  static bool CompareType(const ColumnSchema &a, const ColumnSchema &b) {
    return CompNullable(a, b) && CompHashKey(a, b) &&
        CompSortingType(a, b) && CompTypeInfo(a, b);
  }

  static bool CompareByDefault(const ColumnSchema &a, const ColumnSchema &b) {
    return CompareType(a, b) && CompName(a, b);
  }

  // name: column name
  // type: column type (e.g. UINT8, INT32, STRING, MAP<INT32, STRING> ...)
  // is_nullable: true if a row value can be null
  // is_hash_key: true if a column's hash value can be used for partitioning.
  //
  // Example:
  //   ColumnSchema col_a("a", UINT32)
  //   ColumnSchema col_b("b", STRING, true);
  //   uint32_t default_i32 = -15;
  //   ColumnSchema col_c("c", INT32, false, &default_i32);
  //   Slice default_str("Hello");
  //   ColumnSchema col_d("d", STRING, false, &default_str);
  ColumnSchema(std::string name,
               const std::shared_ptr<QLType>& type,
               bool is_nullable = false,
               bool is_hash_key = false,
               bool is_static = false,
               bool is_counter = false,
               int32_t order = 0,
               SortingType sorting_type = SortingType::kNotSpecified,
               int32_t pg_type_oid = 0 /*kInvalidOid*/)
      : name_(std::move(name)),
        type_(type),
        is_nullable_(is_nullable),
        is_hash_key_(is_hash_key),
        is_static_(is_static),
        is_counter_(is_counter),
        order_(order),
        sorting_type_(sorting_type),
        pg_type_oid_(pg_type_oid) {
  }

  // convenience constructor for creating columns with simple (non-parametric) data types
  ColumnSchema(std::string name,
               DataType type,
               bool is_nullable = false,
               bool is_hash_key = false,
               bool is_static = false,
               bool is_counter = false,
               int32_t order = 0,
               SortingType sorting_type = SortingType::kNotSpecified,
               int32_t pg_type_oid = 0 /*kInvalidOid*/);

  const std::shared_ptr<QLType>& type() const {
    return type_;
  }

  void set_type(const std::shared_ptr<QLType>& type) {
    type_ = type;
  }

  const TypeInfo* type_info() const;

  bool is_nullable() const {
    return is_nullable_;
  }

  bool is_hash_key() const {
    return is_hash_key_;
  }

  bool is_static() const {
    return is_static_;
  }

  bool is_counter() const {
    return is_counter_;
  }

  bool is_collection() const;

  int32_t order() const {
    return order_;
  }

  int32_t pg_type_oid() const {
    return pg_type_oid_;
  }

  void set_pg_type_oid(uint32_t pg_type_oid) {
    pg_type_oid_ = pg_type_oid;
  }

  SortingType sorting_type() const {
    return sorting_type_;
  }

  void set_sorting_type(SortingType sorting_type) {
    sorting_type_ = sorting_type;
  }

  const std::string sorting_type_string() const {
    switch (sorting_type_) {
      case kNotSpecified:
        return "none";
      case kAscending:
        return "asc";
      case kDescending:
        return "desc";
      case kAscendingNullsLast:
        return "asc nulls last";
      case kDescendingNullsLast:
        return "desc nulls last";
    }
    LOG(FATAL) << "Invalid sorting type: " << sorting_type_;
  }

  const std::string &name() const {
    return name_;
  }

  // Return a string identifying this column, including its
  // name.
  std::string ToString() const;

  // Same as above, but only including the type information.
  // For example, "STRING NOT NULL".
  std::string TypeToString() const;

  template <typename Comparator>
  bool Equals(const ColumnSchema &other, Comparator comp) const {
    return comp(*this, other);
  }

  bool EqualsType(const ColumnSchema &other) const {
    return Equals(other, CompareType);
  }

  bool Equals(const ColumnSchema &other) const {
    return Equals(other, CompareByDefault);
  }

  int Compare(const void *lhs, const void *rhs) const;

  // Stringify the given cell. This just stringifies the cell contents,
  // and doesn't include the column name or type.
  std::string Stringify(const void *cell) const;

  // Append a debug string for this cell. This differs from Stringify above
  // in that it also includes the column info, for example 'STRING foo=bar'.
  template<class CellType>
  void DebugCellAppend(const CellType& cell, std::string* ret) const {
    DoDebugCellAppend((is_nullable_ && cell.is_null()) ? nullptr : cell.ptr(), ret);
  }

  // Returns the memory usage of this object without the object itself. Should
  // be used when embedded inside another object.
  size_t memory_footprint_excluding_this() const;

  // Returns the memory usage of this object including the object itself.
  // Should be used when allocated on the heap.
  size_t memory_footprint_including_this() const;

  // Should account for every field in ColumnSchema.
  static bool TEST_Equals(const ColumnSchema& lhs, const ColumnSchema& rhs);

 private:
  friend class SchemaBuilder;

  void set_name(const std::string& name) {
    name_ = name;
  }

  void DoDebugCellAppend(const void* cell, std::string* ret) const;

  std::string name_;
  std::shared_ptr<QLType> type_;
  bool is_nullable_;
  bool is_hash_key_;
  bool is_static_;
  bool is_counter_;
  int32_t order_;
  SortingType sorting_type_;
  int32_t pg_type_oid_;
};

class ContiguousRow;

inline constexpr uint32_t kCurrentPartitioningVersion = 1;

class TableProperties {
 public:
  inline TableProperties() {
    Reset();
  }

  // Containing counters is a internal property instead of a user-defined property, so we don't use
  // it when comparing table properties.
  bool operator==(const TableProperties& other) const {
    if (!Equivalent(other)) {
      return false;
    }

    return default_time_to_live_ == other.default_time_to_live_ &&
           use_mangled_column_name_ == other.use_mangled_column_name_ &&
           contain_counters_ == other.contain_counters_;

    // Ignoring num_tablets_.
    // Ignoring retain_delete_markers_.
    // Ignoring partitioning_version_.
  }

  bool operator!=(const TableProperties& other) const {
    return !(*this == other);
  }

  bool Equivalent(const TableProperties& other) const {
    if (is_ysql_catalog_table_ != other.is_ysql_catalog_table_) {
      return false;
    }

    if (is_transactional_ != other.is_transactional_) {
      return false;
    }

    if (consistency_level_ != other.consistency_level_) {
      return false;
    }

    // Ignoring default_time_to_live_.
    // Ignoring num_tablets_.
    // Ignoring use_mangled_column_name_.
    // Ignoring contain_counters_.
    // Ignoring retain_delete_markers_.
    // Ignoring partitioning_version_.
    return true;
  }

  bool HasDefaultTimeToLive() const {
    return (default_time_to_live_ != kNoDefaultTtl);
  }

  void SetDefaultTimeToLive(uint64_t default_time_to_live) {
    default_time_to_live_ = default_time_to_live;
  }

  int64_t DefaultTimeToLive() const {
    return default_time_to_live_;
  }

  bool contain_counters() const {
    return contain_counters_;
  }

  bool is_transactional() const {
    return is_transactional_;
  }

  YBConsistencyLevel consistency_level() const {
    return consistency_level_;
  }

  void SetContainCounters(bool contain_counters) {
    contain_counters_ = contain_counters;
  }

  void SetTransactional(bool is_transactional) {
    is_transactional_ = is_transactional;
  }

  void SetConsistencyLevel(YBConsistencyLevel consistency_level) {
    consistency_level_ = consistency_level;
  }

  void SetUseMangledColumnName(bool value) {
    use_mangled_column_name_ = value;
  }

  bool use_mangled_column_name() const {
    return use_mangled_column_name_;
  }

  void SetNumTablets(int num_tablets) {
    num_tablets_ = num_tablets;
  }

  bool HasNumTablets() const {
    return num_tablets_ > 0;
  }

  int num_tablets() const {
    return num_tablets_;
  }

  void set_is_ysql_catalog_table(bool is_ysql_catalog_table) {
    is_ysql_catalog_table_ = is_ysql_catalog_table;
  }

  bool is_ysql_catalog_table() const {
    return is_ysql_catalog_table_;
  }

  bool retain_delete_markers() const {
    return retain_delete_markers_;
  }

  void SetRetainDeleteMarkers(bool retain_delete_markers) {
    retain_delete_markers_ = retain_delete_markers;
  }

  uint32_t partitioning_version() const {
    return partitioning_version_;
  }

  void set_partitioning_version(uint32_t value) {
    partitioning_version_ = value;
  }

  void ToTablePropertiesPB(TablePropertiesPB *pb) const;

  static TableProperties FromTablePropertiesPB(const TablePropertiesPB& pb);

  void AlterFromTablePropertiesPB(const TablePropertiesPB& pb);

  void Reset();

  std::string ToString() const;

 private:
  // IMPORTANT: Every time a new property is added, we need to revisit
  // operator== and Equivalent methods to make sure that the new property
  // is being taken into consideration when deciding whether properties between
  // two different tables are equal or equivalent.
  int64_t default_time_to_live_;
  bool contain_counters_;
  bool is_transactional_;
  bool retain_delete_markers_;
  YBConsistencyLevel consistency_level_;
  bool use_mangled_column_name_;
  int num_tablets_;
  bool is_ysql_catalog_table_;
  uint32_t partitioning_version_;
};

typedef std::string PgSchemaName;

// Used to store the offsets of components of a row key (DocKey).
// hash_part_size - size of the hash part of the key.
// doc_key_size - size of the hash part + range part of the key.
// key_offsets[num_of_key_cols] - each element contains the start offset of corresponding key
// column.
struct DocKeyOffsets {
  size_t hash_part_size;
  size_t doc_key_size;
  std::vector<size_t> key_offsets;
};

// The schema for a set of rows.
//
// A Schema is simply a set of columns, along with information about
// which prefix of columns makes up the primary key.
//
// Note that, while Schema is copyable and assignable, it is a complex
// object that is not inexpensive to copy. You should generally prefer
// passing by pointer or reference, and functions that create new
// Schemas should generally prefer taking a Schema pointer and using
// Schema::swap() or Schema::Reset() rather than returning by value.
class Schema {
 public:
  static constexpr ssize_t kColumnNotFound = -1;

  Schema()
    : num_key_columns_(0),
      num_hash_key_columns_(0),
      // TODO: C++11 provides a single-arg constructor
      name_to_index_(10,
                     NameToIndexMap::hasher(),
                     NameToIndexMap::key_equal(),
                     NameToIndexMapAllocator(&name_to_index_bytes_)),
      has_nullables_(false),
      cotable_id_(Uuid::Nil()),
      colocation_id_(kColocationIdNotSet),
      pgschema_name_("") {
  }

  Schema(const Schema& other);
  Schema& operator=(const Schema& other);

  void swap(Schema& other); // NOLINT(build/include_what_you_use)

  void CopyFrom(const Schema& other);

  // Construct a schema with the given information.
  //
  // NOTE: if the schema is user-provided, it's better to construct an
  // empty schema and then use Reset(...)  so that errors can be
  // caught. If an invalid schema is passed to this constructor, an
  // assertion will be fired!
  Schema(const std::vector<ColumnSchema>& cols,
         size_t key_columns,
         const TableProperties& table_properties = TableProperties(),
         const Uuid& cotable_id = Uuid::Nil(),
         const ColocationId colocation_id = kColocationIdNotSet,
         const PgSchemaName pgschema_name = "");

  // Construct a schema with the given information.
  //
  // NOTE: if the schema is user-provided, it's better to construct an
  // empty schema and then use Reset(...)  so that errors can be
  // caught. If an invalid schema is passed to this constructor, an
  // assertion will be fired!
  Schema(const std::vector<ColumnSchema>& cols,
         const std::vector<ColumnId>& ids,
         size_t key_columns,
         const TableProperties& table_properties = TableProperties(),
         const Uuid& cotable_id = Uuid::Nil(),
         const ColocationId colocation_id = kColocationIdNotSet,
         const PgSchemaName pgschema_name = "");

  // Reset this Schema object to the given schema.
  // If this fails, the Schema object is left in an inconsistent
  // state and may not be used.
  Status Reset(const std::vector<ColumnSchema>& cols, size_t key_columns,
               const TableProperties& table_properties = TableProperties(),
               const Uuid& cotable_id = Uuid::Nil(),
               const ColocationId colocation_id = kColocationIdNotSet,
               const PgSchemaName pgschema_name = "");

  // Reset this Schema object to the given schema.
  // If this fails, the Schema object is left in an inconsistent
  // state and may not be used.
  Status Reset(const std::vector<ColumnSchema>& cols,
               const std::vector<ColumnId>& ids,
               size_t key_columns,
               const TableProperties& table_properties = TableProperties(),
               const Uuid& cotable_id = Uuid::Nil(),
               const ColocationId colocation_id = kColocationIdNotSet,
               const PgSchemaName pgschema_name = "");

  // Recompute the dockey offsets if they were already set. This is used
  // from set_colocation_id and set_cotable_id which are the only methods which
  // can change the encoded dockey format after Schema is created.
  void UpdateDocKeyOffsets();

  // Return the number of bytes needed to represent a single row of this schema.
  //
  // This size does not include any indirected (variable length) data (eg strings)
  size_t byte_size() const {
    DCHECK(initialized());
    return col_offsets_.back();
  }

  // Return the number of columns in this schema
  size_t num_columns() const {
    return cols_.size();
  }

  // Return the length of the key prefix in this schema.
  size_t num_key_columns() const {
    return num_key_columns_;
  }

  // Number of hash key columns.
  size_t num_hash_key_columns() const {
    return num_hash_key_columns_;
  }

  // Number of range key columns.
  size_t num_range_key_columns() const {
    return num_key_columns_ - num_hash_key_columns_;
  }

  // Return the byte offset within the row for the given column index.
  size_t column_offset(size_t col_idx) const {
    DCHECK_LT(col_idx, cols_.size());
    return col_offsets_[col_idx];
  }

  // Return optional dockey offset.
  const std::optional<DocKeyOffsets>& doc_key_offsets() const {
    return doc_key_offsets_;
  }

  // Return the ColumnSchema corresponding to the given column index.
  inline const ColumnSchema &column(size_t idx) const {
    DCHECK_LT(idx, cols_.size());
    return cols_[idx];
  }

  // Return the ColumnSchema corresponding to the given column ID.
  Result<const ColumnSchema&> column_by_id(ColumnId id) const;

  // Return the column ID corresponding to the given column index
  ColumnId column_id(size_t idx) const {
    DCHECK(has_column_ids());
    DCHECK_LT(idx, cols_.size());
    return col_ids_[idx];
  }

  // Return true if the schema contains an ID mapping for its columns.
  // In the case of an empty schema, this is false.
  bool has_column_ids() const {
    return !col_ids_.empty();
  }

  const std::vector<ColumnSchema>& columns() const {
    return cols_;
  }

  const std::vector<ColumnId>& column_ids() const {
    return col_ids_;
  }

  const std::vector<std::string> column_names() const {
    std::vector<std::string> column_names;
    for (const auto& col : cols_) {
      column_names.push_back(col.name());
    }
    return column_names;
  }

  const TableProperties& table_properties() const {
    return table_properties_;
  }

  TableProperties* mutable_table_properties() {
    return &table_properties_;
  }

  void SetDefaultTimeToLive(const uint64_t& ttl_msec) {
    table_properties_.SetDefaultTimeToLive(ttl_msec);
  }

  void SetTransactional(bool is_transactional) {
    table_properties_.SetTransactional(is_transactional);
  }

  void SetRetainDeleteMarkers(bool retain_delete_markers) {
    table_properties_.SetRetainDeleteMarkers(retain_delete_markers);
  }

  bool has_pgschema_name() const {
    return !pgschema_name_.empty();
  }

  void SetSchemaName(std::string pgschema_name) {
    pgschema_name_ = pgschema_name;
  }

  PgSchemaName SchemaName() const {
    return pgschema_name_;
  }

  // Return the column index corresponding to the given column,
  // or kColumnNotFound if the column is not in this schema.
  ssize_t find_column(const GStringPiece col_name) const {
    auto iter = name_to_index_.find(col_name);
    if (PREDICT_FALSE(iter == name_to_index_.end())) {
      return kColumnNotFound;
    } else {
      return iter->second;
    }
  }

  Result<ColumnId> ColumnIdByName(const std::string& name) const;

  Result<ssize_t> ColumnIndexByName(GStringPiece col_name) const;

  // Returns true if the schema contains nullable columns
  bool has_nullables() const {
    return has_nullables_;
  }

  // Returns true if the schema contains static columns
  bool has_statics() const {
    return has_statics_;
  }

  // Returns true if the specified column (by index) is a key
  bool is_key_column(size_t idx) const {
    return idx < num_key_columns_;
  }

  // Returns true if the specified column (by column id) is a key
  bool is_key_column(ColumnId column_id) const {
    return is_key_column(find_column_by_id(column_id));
  }

  // Returns true if the specified column (by name) is a key
  bool is_key_column(const GStringPiece col_name) const {
    return is_key_column(find_column(col_name));
  }

  // Returns true if the specified column (by index) is a hash key
  bool is_hash_key_column(size_t idx) const {
    return idx < num_hash_key_columns_;
  }

  // Returns true if the specified column (by column id) is a hash key
  bool is_hash_key_column(ColumnId column_id) const {
    return is_hash_key_column(find_column_by_id(column_id));
  }

  // Returns true if the specified column (by name) is a hash key
  bool is_hash_key_column(const GStringPiece col_name) const {
    return is_hash_key_column(find_column(col_name));
  }

  // Returns true if the specified column (by index) is a range column
  bool is_range_column(size_t idx) const {
    return is_key_column(idx) && !is_hash_key_column(idx);
  }

  // Returns true if the specified column (by column id) is a range column
  bool is_range_column(ColumnId column_id) const {
    return is_range_column(find_column_by_id(column_id));
  }

  // Returns true if the specified column (by name) is a range column
  bool is_range_column(const GStringPiece col_name) const {
    return is_range_column(find_column(col_name));
  }

  // Return true if this Schema is initialized and valid.
  bool initialized() const {
    return !cols_.empty();
  }

  // Returns the highest column id in this Schema.
  ColumnId max_col_id() const {
    return max_col_id_;
  }

  // Gets and sets the uuid of the non-primary table this schema belongs to co-located in a tablet.
  const Uuid& cotable_id() const {
    return cotable_id_;
  }

  bool has_cotable_id() const {
    return !cotable_id_.IsNil();
  }

  void set_cotable_id(const Uuid& cotable_id) {
    if (!cotable_id.IsNil()) {
      DCHECK_EQ(colocation_id_, kColocationIdNotSet);
    }
    cotable_id_ = cotable_id;
    UpdateDocKeyOffsets();
  }

  bool has_yb_hash_code() const {
    return num_hash_key_columns() > 0;
  }

  size_t num_dockey_components() const {
    return num_key_columns() + has_yb_hash_code();
  }

  size_t get_dockey_component_idx(size_t col_idx) const {
    return col_idx == kYbHashCodeColId ? 0 : col_idx + has_yb_hash_code();
  }

  // Gets the colocation ID of the non-primary table this schema belongs to in a
  // tablet with colocated tables.
  ColocationId colocation_id() const {
    return colocation_id_;
  }

  bool has_colocation_id() const {
    return colocation_id_ != kColocationIdNotSet;
  }

  void set_colocation_id(const ColocationId colocation_id) {
    if (colocation_id != kColocationIdNotSet) {
      DCHECK(cotable_id_.IsNil());
    }
    colocation_id_ = colocation_id;
    UpdateDocKeyOffsets();
  }

  bool is_colocated() const {
    return has_colocation_id() || has_cotable_id();
  }

  // Stringify the given row, which conforms to this schema,
  // in a way suitable for debugging. This isn't currently optimized
  // so should be avoided in hot paths.
  template<class RowType>
  std::string DebugRow(const RowType& row) const {
    DCHECK_SCHEMA_EQ(*this, *row.schema());
    return DebugRowColumns(row, num_columns());
  }

  // Compare two rows of this schema.
  template<class RowTypeA, class RowTypeB>
  int Compare(const RowTypeA& lhs, const RowTypeB& rhs) const {
    DCHECK(KeyEquals(*lhs.schema()) && KeyEquals(*rhs.schema()));

    for (size_t col = 0; col < num_key_columns_; col++) {
      int col_compare = column(col).Compare(lhs.cell_ptr(col), rhs.cell_ptr(col));
      if (col_compare != 0) {
        return col_compare;
      }
    }
    return 0;
  }

  // Return the projection of this schema which contains only
  // the key columns.
  // TODO: this should take a Schema* out-parameter to avoid an
  // extra copy of the ColumnSchemas.
  // TODO this should probably be cached since the key projection
  // is not supposed to change, for a single schema.
  Schema CreateKeyProjection() const {
    std::vector<ColumnSchema> key_cols(cols_.begin(),
                                  cols_.begin() + num_key_columns_);
    std::vector<ColumnId> col_ids;
    if (!col_ids_.empty()) {
      col_ids.assign(col_ids_.begin(), col_ids_.begin() + num_key_columns_);
    }

    return Schema(key_cols, col_ids, num_key_columns_);
  }

  // Initialize column IDs by default values.
  // Requires that this schema has no column IDs.
  void InitColumnIdsByDefault();

  // Return a new Schema which is the same as this one, but without any column
  // IDs assigned.
  //
  // Requires that this schema has column IDs.
  Schema CopyWithoutColumnIds() const;

  // Create a new schema containing only the selected columns.
  // The resulting schema will have no key columns defined.
  // If this schema has IDs, the resulting schema will as well.
  Status CreateProjectionByNames(const std::vector<GStringPiece>& col_names,
                                 Schema* out, size_t num_key_columns = 0) const;

  // Create a new schema containing only the selected column IDs.
  //
  // If any column IDs are invalid, then they will be ignored and the
  // result will have fewer columns than requested.
  //
  // The resulting schema will have no key columns defined.
  Status CreateProjectionByIdsIgnoreMissing(const std::vector<ColumnId>& col_ids,
                                            Schema* out) const;

  // Stringify this Schema. This is not particularly efficient,
  // so should only be used when necessary for output.
  std::string ToString() const;

  // Return true if the schemas have exactly the same set of columns
  // and respective types, and the same table properties.
  template <typename ColumnComparator>
  bool Equals(const Schema &other, ColumnComparator comp) const {
    if (this == &other) return true;
    if (this->num_key_columns_ != other.num_key_columns_) return false;
    if (this->table_properties_ != other.table_properties_) return false;
    if (this->cols_.size() != other.cols_.size()) return false;

    for (size_t i = 0; i < other.cols_.size(); i++) {
      if (!this->cols_[i].Equals(other.cols_[i], comp)) return false;
    }

    return true;
  }

  bool Equals(const Schema &other) const {
    return Equals(other, ColumnSchema::CompareByDefault);
  }

  // Return true if this schema is a subset of the source. The set of columns and respective types
  // should match exactly
  bool IsSubsetOf(const Schema& source) const {
    if (this == &source) return true;
    if (this->num_key_columns_ != source.num_key_columns_) return false;
    if (!this->table_properties_.Equivalent(source.table_properties_)) return false;
    if (this->cols_.size() < source.cols_.size()) return false;

    for (size_t i = 0; i < source.cols_.size(); i++) {
      if (!this->cols_[i].Equals(source.cols_[i])) return false;
      if (this->column_id(i) != source.column_id(i)) return false;
    }

    return true;
  }

  // Return true if this schema has exactly the same set of columns and respective types, and
  // equivalent properties as the source.  The source must be an equivalent of this object.
  // With Packed columns, number of columns of the source and this object also need to match
  // for equivalency
  bool EquivalentForDataCopy(const Schema& source) const {
    return (this->cols_.size() == source.cols_.size()) && IsSubsetOf(source);
  }

  // Return true if the key projection schemas have exactly the same set of
  // columns and respective types. Doesn't check column names.
  bool KeyEquals(const Schema& other) const {
    if (this->num_key_columns_ != other.num_key_columns_) return false;
    for (size_t i = 0; i < this->num_key_columns_; i++) {
      if (!this->cols_[i].EqualsType(other.cols_[i])) return false;
    }
    return true;
  }

  // Return a non-OK status if the project is not compatible with the current schema
  // - User columns non present in the tablet are considered errors
  // - Matching columns with different types, at the moment, are considered errors
  Status VerifyProjectionCompatibility(const Schema& projection) const;

  // Returns the projection schema mapped on the current one
  // If the project is invalid, return a non-OK status.
  Status GetMappedReadProjection(const Schema& projection,
                                 Schema *mapped_projection) const;

  // Loops through this schema (the projection) and calls the projector methods once for
  // each column.
  //
  // - Status ProjectBaseColumn(size_t proj_col_idx, size_t base_col_idx)
  //
  //     Called if the column in this schema matches one of the columns in 'base_schema'.
  //     The column type must match exactly.
  //
  // - Status ProjectDefaultColumn(size_t proj_idx)
  //
  //     Called if the column in this schema does not match any column in 'base_schema',
  //     but has a default or is nullable.
  //
  // - Status ProjectExtraColumn(size_t proj_idx, const ColumnSchema& col)
  //
  //     Called if the column in this schema does not match any column in 'base_schema',
  //     and does not have a default, and is not nullable.
  //
  // If both schemas have column IDs, then the matching is done by ID. Otherwise, it is
  // done by name.
  //
  // TODO(MAYBE): Pass the ColumnSchema and not only the column index?
  template <class Projector>
  Status GetProjectionMapping(const Schema& base_schema, Projector *projector) const {
    const bool use_column_ids = base_schema.has_column_ids() && has_column_ids();

    int proj_idx = 0;
    for (size_t i = 0; i < cols_.size(); ++i) {
      const ColumnSchema& col_schema = cols_[i];

      // try to lookup the column by ID if present or just by name.
      // Unit tests and Iter-Projections are probably always using the
      // lookup by name. The IDs are generally set by the server on AlterTable().
      ssize_t base_idx;
      if (use_column_ids) {
        base_idx = base_schema.find_column_by_id(col_ids_[i]);
      } else {
        base_idx = base_schema.find_column(col_schema.name());
      }

      if (base_idx >= 0) {
        const ColumnSchema& base_col_schema = base_schema.column(base_idx);
        // Column present in the Base Schema...
        if (!col_schema.EqualsType(base_col_schema)) {
          // ...but with a different type, (TODO: try with an adaptor)
          return STATUS(InvalidArgument, "The column '" + col_schema.name() +
                                         "' must have type " +
                                         base_col_schema.TypeToString() +
                                         " found " + col_schema.TypeToString());
        } else {
          RETURN_NOT_OK(projector->ProjectBaseColumn(proj_idx, base_idx));
        }
      } else {
        if (!col_schema.is_nullable()) {
          RETURN_NOT_OK(projector->ProjectExtraColumn(proj_idx));
        }
      }
      proj_idx++;
    }
    return Status::OK();
  }

  // Returns the column index given the column ID.
  // If no such column exists, returns kColumnNotFound.
  int find_column_by_id(ColumnId id) const {
    DCHECK(cols_.empty() || has_column_ids());
    int ret = id_to_index_[id];
    if (ret == -1) {
      return kColumnNotFound;
    }
    return ret;
  }

  // Returns the memory usage of this object without the object itself. Should
  // be used when embedded inside another object.
  size_t memory_footprint_excluding_this() const;

  // Returns the memory usage of this object including the object itself.
  // Should be used when allocated on the heap.
  size_t memory_footprint_including_this() const;

  static ColumnId first_column_id();

  // Should account for every field in Schema.
  // TODO: Some of them should be in Equals too?
  static bool TEST_Equals(const Schema& lhs, const Schema& rhs);

 private:

  void ResetColumnIds(const std::vector<ColumnId>& ids);

  // Return a stringified version of the first 'num_columns' columns of the
  // row.
  template<class RowType>
  std::string DebugRowColumns(const RowType& row, size_t num_columns) const {
    std::string ret;
    ret.append("(");

    for (size_t col_idx = 0; col_idx < num_columns; col_idx++) {
      if (col_idx > 0) {
        ret.append(", ");
      }
      const ColumnSchema& col = cols_[col_idx];
      col.DebugCellAppend(row.cell(col_idx), &ret);
    }
    ret.append(")");
    return ret;
  }

  friend class SchemaBuilder;

  std::vector<ColumnSchema> cols_;
  size_t num_key_columns_;
  size_t num_hash_key_columns_;
  ColumnId max_col_id_;
  std::vector<ColumnId> col_ids_;
  std::vector<size_t> col_offsets_;
  std::optional<DocKeyOffsets> doc_key_offsets_;

  // The keys of this map are GStringPiece references to the actual name members of the
  // ColumnSchema objects inside cols_. This avoids an extra copy of those strings,
  // and also allows us to do lookups on the map using GStringPiece keys, sometimes
  // avoiding copies.
  //
  // The map is instrumented with a counting allocator so that we can accurately
  // measure its memory footprint.
  int64_t name_to_index_bytes_ = 0;
  typedef STLCountingAllocator<std::pair<const GStringPiece, size_t>> NameToIndexMapAllocator;
  typedef std::unordered_map<
      GStringPiece,
      size_t,
      std::hash<GStringPiece>,
      std::equal_to<GStringPiece>,
      NameToIndexMapAllocator> NameToIndexMap;
  NameToIndexMap name_to_index_;

  IdMapping id_to_index_;

  // Cached indicator whether any columns are nullable.
  bool has_nullables_;

  // Cached indicator whether any columns are static.
  bool has_statics_ = false;

  TableProperties table_properties_;

  // Uuid of the non-primary table this schema belongs to co-located in a tablet. Nil for the
  // primary or single-tenant table.
  Uuid cotable_id_;

  // Colocation ID used to distinguish a table within a colocation group.
  // kColocationIdNotSet for a primary or single-tenant table.
  ColocationId colocation_id_;

  PgSchemaName pgschema_name_;

  // NOTE: if you add more members, make sure to add the appropriate
  // code to swap() and CopyFrom() as well to prevent subtle bugs.
};

// Helper used for schema creation/editing.
//
// Example:
//   Status s;
//   SchemaBuilder builder(base_schema);
//   s = builder.RemoveColumn("value");
//   s = builder.AddKeyColumn("key2", STRING);
//   s = builder.AddColumn("new_c1", UINT32);
//   ...
//   Schema new_schema = builder.Build();
//
// TODO(neil): Must introduce hash_key in this builder. Currently, only YBSchemaBuilder support
// hash key, and YBSchemaBuilder don't use this builder.
class SchemaBuilder {
 public:
  SchemaBuilder() { Reset(); }
  explicit SchemaBuilder(const Schema& schema) { Reset(schema); }

  void Reset();
  void Reset(const Schema& schema);

  bool is_valid() const { return cols_.size() > 0; }

  // Set the next column ID to be assigned to columns added with
  // AddColumn.
  void set_next_column_id(ColumnId next_id) {
    DCHECK_GE(next_id, ColumnId(0));
    next_id_ = next_id;
  }

  // Return the next column ID that would be assigned with AddColumn.
  ColumnId next_column_id() const {
    return next_id_;
  }

  void set_colocation_id(ColocationId colocation_id) {
    colocation_id_ = colocation_id;
  }

  ColocationId colocation_id() const {
    return colocation_id_;
  }

  void set_pgschema_name(PgSchemaName pgschema_name) {
    pgschema_name_ = pgschema_name;
  }

  PgSchemaName pgschema_name() const {
    return pgschema_name_;
  }

  void set_cotable_id(Uuid cotable_id) {
    cotable_id_ = cotable_id;
  }

  Uuid cotable_id() const {
    return cotable_id_;
  }

  Schema Build() const {
    return Schema(cols_, col_ids_, num_key_columns_, table_properties_, cotable_id_,
                  colocation_id_, pgschema_name_);
  }
  Schema BuildWithoutIds() const {
    return Schema(cols_, num_key_columns_, table_properties_, cotable_id_,
                  colocation_id_, pgschema_name_);
  }

  // assumes type is allowed in primary key -- this should be checked before getting here
  // using DataType (not QLType) since primary key columns only support elementary types
  Status AddKeyColumn(const std::string& name, const std::shared_ptr<QLType>& type);
  Status AddKeyColumn(const std::string& name, DataType type);

  // assumes type is allowed in hash key -- this should be checked before getting here
  // using DataType (not QLType) since hash key columns only support elementary types
  Status AddHashKeyColumn(const std::string& name, const std::shared_ptr<QLType>& type);
  Status AddHashKeyColumn(const std::string& name, DataType type);

  Status AddColumn(const ColumnSchema& column, bool is_key);

  Status AddColumn(const std::string& name, const std::shared_ptr<QLType>& type) {
    return AddColumn(name, type, false, false, false, false, 0,
                     SortingType::kNotSpecified);
  }

  // convenience function for adding columns with simple (non-parametric) data types
  Status AddColumn(const std::string& name, DataType type);

  Status AddNullableColumn(const std::string& name, const std::shared_ptr<QLType>& type) {
    return AddColumn(name, type, true, false, false, false, 0,
                     SortingType::kNotSpecified);
  }

  // convenience function for adding columns with simple (non-parametric) data types
  Status AddNullableColumn(const std::string& name, DataType type);

  Status AddColumn(const std::string& name,
                   const std::shared_ptr<QLType>& type,
                   bool is_nullable,
                   bool is_hash_key,
                   bool is_static,
                   bool is_counter,
                   int32_t order,
                   yb::SortingType sorting_type);

  // convenience function for adding columns with simple (non-parametric) data types
  Status AddColumn(const std::string& name,
                   DataType type,
                   bool is_nullable,
                   bool is_hash_key,
                   bool is_static,
                   bool is_counter,
                   int32_t order,
                   yb::SortingType sorting_type);

  Status RemoveColumn(const std::string& name);
  Status RenameColumn(const std::string& old_name, const std::string& new_name);
  Status SetColumnPGType(const std::string& name, const uint32_t pg_type_oid);
  Status AlterProperties(const TablePropertiesPB& pb);

 private:
  ColumnId next_id_;
  std::vector<ColumnId> col_ids_;
  std::vector<ColumnSchema> cols_;
  std::unordered_set<std::string> col_names_;
  size_t num_key_columns_;
  TableProperties table_properties_;
  ColocationId colocation_id_ = kColocationIdNotSet;
  PgSchemaName pgschema_name_ = "";
  Uuid cotable_id_ = Uuid::Nil();

  DISALLOW_COPY_AND_ASSIGN(SchemaBuilder);
};
} // namespace yb

// Specialize std::hash for ColumnId
namespace std {
template<>
struct hash<yb::ColumnId> {
  int operator()(const yb::ColumnId& col_id) const {
    return col_id;
  }
};
} // namespace std
