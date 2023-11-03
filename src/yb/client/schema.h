//
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
// This module defines the schema that will be used when creating tables.
//
// Note on primary key definitions.
// - There are two different APIs to define primary key. They cannot be used together but can be
//   used interchangeably for the same purpose (This is different from Kudu's original design which
//   uses one API for single-column key and another for multi-column key).
// - First API:
//   Each column of a primary key can be specified as hash or regular primary key.
//   Function PrimaryKey()
//   Function HashPrimaryKey().
#pragma once

#include <string>
#include <vector>

#include "yb/common/constants.h"
#include "yb/common/schema.h"
#include "yb/common/pg_types.h"
#include "yb/common/common_types.pb.h"
#include "yb/common/value.pb.h"

#include "yb/client/client_fwd.h"
#include "yb/client/value.h"

#include "yb/dockv/dockv_fwd.h"

#include "yb/util/status_fwd.h"

namespace yb {

// the types used internally and sent over the wire to the tserver
typedef QLValuePB::ValueCase InternalType;

namespace client {

namespace internal {

const Schema& GetSchema(const YBSchema& schema);
Schema& GetSchema(YBSchema* schema);

} // namespace internal

class YBClient;
class YBSchema;
class YBSchemaBuilder;
class YBOperation;

class YBColumnSchema {
 public:
  static InternalType ToInternalDataType(DataType type);
  static InternalType ToInternalDataType(const std::shared_ptr<QLType>& ql_type);

  YBColumnSchema(const std::string &name,
                 const std::shared_ptr<QLType>& type,
                 ColumnKind kind = ColumnKind::VALUE,
                 Nullable is_nullable = Nullable::kFalse,
                 bool is_static = false,
                 bool is_counter = false,
                 int32_t order = 0,
                 int32_t pg_type_oid = kPgInvalidOid,
                 const QLValuePB& missing_value = QLValuePB());
  YBColumnSchema(const YBColumnSchema& other);
  ~YBColumnSchema();

  YBColumnSchema& operator=(const YBColumnSchema& other);

  void CopyFrom(const YBColumnSchema& other);

  bool Equals(const YBColumnSchema& other) const;

  // Getters to expose column schema information.
  const std::string& name() const;
  const std::shared_ptr<QLType>& type() const;
  bool is_key() const;
  bool is_hash_key() const;
  bool is_nullable() const;
  bool is_static() const;
  bool is_counter() const;
  int32_t order() const;
  int32_t pg_type_oid() const;
  SortingType sorting_type() const;

 private:
  friend class YBColumnSpec;
  friend class YBSchema;
  friend class YBSchemaBuilder;
  // YBTableAlterer::Data needs to be a friend. Friending the parent class
  // is transitive to nested classes. See http://tiny.cloudera.com/jwtui
  friend class YBTableAlterer;

  YBColumnSchema();

  std::unique_ptr<ColumnSchema> col_;
};

// Builder API for specifying or altering a column within a table schema.
// This cannot be constructed directly, but rather is returned from
// YBSchemaBuilder::AddColumn() to specify a column within a Schema.
//
// TODO(KUDU-861): this API will also be used for an improved AlterTable API.
class YBColumnSpec {
 public:
  explicit YBColumnSpec(const std::string& col_name);

  ~YBColumnSpec();

  // Operations only relevant for Create Table
  // ------------------------------------------------------------

  // Set this column to be the primary key of the table.
  //
  // Only relevant for a CreateTable operation. Primary keys may not be changed
  // after a table is created.
  YBColumnSpec* PrimaryKey(SortingType sorting_type = SortingType::kAscending);

  // Set this column to be a hash primary key column of the table. A hash value of all hash columns
  // in the primary key will be used to determine what partition (tablet) a particular row falls in.
  YBColumnSpec* HashPrimaryKey();

  // Set this column to be static. A static column is a column whose value is shared among rows of
  // the same hash key.
  YBColumnSpec* StaticColumn();

  // Set this column to be not nullable.
  // Column nullability may not be changed once a table is created.
  YBColumnSpec* NotNull();

  // Set this column to be nullable (the default).
  // Column nullability may not be changed once a table is created.
  YBColumnSpec* Nullable();

  // Set the type of this column.
  // Column types may not be changed once a table is created.
  YBColumnSpec* Type(const std::shared_ptr<QLType>& type);

  // Convenience function for setting a simple (i.e. non-parametric) data type.
  YBColumnSpec* Type(DataType type);

  // Specify the user-defined order of the column.
  YBColumnSpec* Order(int32_t order);

  // Identify this column as counter.
  YBColumnSpec* Counter();

  // PgTypeOid
  YBColumnSpec* PgTypeOid(int32_t oid);

  // Add JSON operation.
  YBColumnSpec* JsonOp(JsonOperatorPB op, const std::string& str_value);
  YBColumnSpec* JsonOp(JsonOperatorPB op, int32_t int_value);

  // Operations only relevant for Alter Table
  // ------------------------------------------------------------

  // Rename this column.
  YBColumnSpec* RenameTo(const std::string& new_name);

  YBColumnSpec* SetMissing(const QLValuePB& missing_value);

 private:
  class Data;
  friend class YBSchemaBuilder;
  friend class YBTableAlterer;

  Status ToColumnSchema(YBColumnSchema* col) const;

  YBColumnSpec* JsonOp(JsonOperatorPB op, const QLValuePB& value);

  // Owned.
  Data* data_;
};

// Builder API for constructing a YBSchema object.
// The API here is a "fluent" style of programming, such that the resulting code
// looks somewhat like a SQL "CREATE TABLE" statement. For example:
//
// SQL:
//   CREATE TABLE t (
//     my_key int not null primary key,
//     a float
//   );
//
// is represented as:
//
//   YBSchemaBuilder t;
//   t.AddColumn("my_key")->Type(YBColumnSchema::INT32)->NotNull()->PrimaryKey();
//   t.AddColumn("a")->Type(YBColumnSchema::FLOAT);
//   YBSchema schema;
//   t.Build(&schema);
class YBSchemaBuilder {
 public:
  YBSchemaBuilder();
  ~YBSchemaBuilder();

  // Return a YBColumnSpec for a new column within the Schema.
  // The returned object is owned by the YBSchemaBuilder.
  YBColumnSpec* AddColumn(const std::string& name);

  YBSchemaBuilder* SetTableProperties(const TableProperties& table_properties);

  YBSchemaBuilder* SetSchemaName(const std::string& pgschema_name);

  std::string SchemaName();

  // Resets 'schema' to the result of this builder.
  //
  // If the Schema is invalid for any reason (eg missing types, duplicate column names, etc)
  // a bad Status will be returned.
  Status Build(YBSchema* schema);

 private:
  class Data;
  // Owned.
  Data* data_;
};

class YBSchema {
 public:
  YBSchema();

  explicit YBSchema(const Schema& schema);

  YBSchema(const YBSchema& other);
  YBSchema(YBSchema&& other);
  ~YBSchema();

  YBSchema& operator=(const YBSchema& other);
  YBSchema& operator=(YBSchema&& other);
  void CopyFrom(const YBSchema& other);
  void MoveFrom(YBSchema&& other);

  // DEPRECATED: will be removed soon.
  Status Reset(const std::vector<YBColumnSchema>& columns, const TableProperties& table_properties);

  void Reset(std::unique_ptr<Schema> schema);

  bool Equals(const YBSchema& other) const;

  bool EquivalentForDataCopy(const YBSchema& source_schema) const;

  Result<bool> Equals(const SchemaPB& pb_schema) const;

  // Two schemas are equivalent if it's possible to copy data from the source table to the
  // destination table containing the schema represented by this class. Not a pure Equals. Rules:
  //  1. The source schema must have matching columns and columns types on the destination.
  //  2. Table properties might be different in areas that are not relevant (e.g. TTL).
  Result<bool> EquivalentForDataCopy(const SchemaPB& source_pb_schema) const;

  const TableProperties& table_properties() const;

  YBColumnSchema Column(size_t idx) const;
  YBColumnSchema ColumnById(int32_t id) const;

  // Returns column id provided its index.
  int32_t ColumnId(size_t idx) const;

  // Returns the number of columns in hash primary keys.
  size_t num_hash_key_columns() const;

  // Number of range key columns.
  size_t num_range_key_columns() const;

  // Returns the number of columns in primary keys.
  size_t num_key_columns() const;

  // Returns the total number of columns.
  size_t num_columns() const;

  bool has_colocation_id() const;

  // Gets the colocation ID of the non-primary table this schema belongs to in a
  // tablet with colocated tables.
  ColocationId colocation_id() const;

  uint32_t version() const;
  void set_version(uint32_t version);
  bool is_compatible_with_previous_version() const;
  void set_is_compatible_with_previous_version(bool is_compat);

  // Get the indexes of the primary key columns within this Schema.
  // In current versions of YB, these will always be contiguous column
  // indexes starting with 0. However, in future versions this assumption
  // may not hold, so callers should not assume it is the case.
  std::vector<size_t> GetPrimaryKeyColumnIndexes() const;

  // Create a new row corresponding to this schema.
  //
  // The new row refers to this YBSchema object, so must be destroyed before
  // the YBSchema object.
  //
  // The caller takes ownership of the created row.
  std::unique_ptr<dockv::YBPartialRow> NewRow() const;

  const std::vector<ColumnSchema>& columns() const;

  ssize_t FindColumn(const GStringPiece& name) const;

  std::string ToString() const;

  size_t DynamicMemoryUsage() const {
    return schema_->memory_footprint_including_this() + sizeof(*this);
  }

 private:
  friend YBSchema YBSchemaFromSchema(const Schema& schema);
  friend const Schema& internal::GetSchema(const YBSchema& schema);
  friend Schema& internal::GetSchema(YBSchema* schema);

  std::unique_ptr<Schema> schema_;
  uint32_t version_;
  bool is_compatible_with_previous_version_ = false;
};

inline bool operator==(const YBSchema& lhs, const YBSchema& rhs) {
  return lhs.Equals(rhs);
}

inline std::ostream& operator<<(std::ostream& out, const YBSchema& schema) {
  return out << schema.ToString();
}

} // namespace client
} // namespace yb
