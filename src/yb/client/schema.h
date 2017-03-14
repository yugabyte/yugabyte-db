
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
// - Second API:
//   All hash and regular primary columns can be specified together in a list.
//   Function YBSchemaBuilder::SetPrimaryKey().
#ifndef YB_CLIENT_SCHEMA_H
#define YB_CLIENT_SCHEMA_H

#include <string>
#include <vector>

#include "yb/client/value.h"
#include "yb/util/yb_export.h"
#include "yb/util/status.h"
#include "yb/common/schema.h"
#include "yb/common/yql_value.h"

namespace yb {

// the types used internally and sent over the wire to the tserver
typedef YQLValue::InternalType InternalType;

class ColumnSchema;
class YBPartialRow;
class Schema;
class TestWorkload;
class TableProperties;

namespace tools {
class TsAdminClient;
}

namespace client {

namespace internal {
class GetTableSchemaRpc;
class LookupRpc;
class WriteRpc;
} // namespace internal

class YBClient;
class YBSchema;
class YBSchemaBuilder;
class YBOperation;

class YB_EXPORT YBColumnStorageAttributes {
 public:
  // NOTE: this constructor is deprecated for external use, and will
  // be made private in a future release.
  YBColumnStorageAttributes(EncodingType encoding = AUTO_ENCODING,
                              CompressionType compression = DEFAULT_COMPRESSION,
                              int32_t block_size = 0)
      : encoding_(encoding),
      compression_(compression),
      block_size_(block_size) {
  }

  const EncodingType encoding() const {
    return encoding_;
  }

  const CompressionType compression() const {
    return compression_;
  }

  std::string ToString() const;

 private:
  EncodingType encoding_;
  CompressionType compression_;
  int32_t block_size_;
};

class YB_EXPORT YBColumnSchema {
 public:

  static InternalType ToInternalDataType(DataType data_type) {
    switch (data_type) {
      case INT8:
        return InternalType::kInt8Value;
      case INT16:
        return InternalType::kInt16Value;
      case INT32:
        return InternalType::kInt32Value;
      case INT64:
        return InternalType::kInt64Value;
      case FLOAT:
        return InternalType::kFloatValue;
      case DOUBLE:
        return InternalType::kDoubleValue;
      case STRING:
        return InternalType::kStringValue;
      case TIMESTAMP:
        return InternalType::kTimestampValue;
      case BOOL:
        return InternalType::kBoolValue;

      case NULL_VALUE_TYPE: FALLTHROUGH_INTENDED;
      case BINARY: FALLTHROUGH_INTENDED;
      case DECIMAL: FALLTHROUGH_INTENDED;
      case VARINT: FALLTHROUGH_INTENDED;
      case INET: FALLTHROUGH_INTENDED;
      case LIST: FALLTHROUGH_INTENDED;
      case MAP: FALLTHROUGH_INTENDED;
      case SET: FALLTHROUGH_INTENDED;
      case UUID: FALLTHROUGH_INTENDED;
      case TIMEUUID: FALLTHROUGH_INTENDED;
      case TUPLE: FALLTHROUGH_INTENDED;
      case TYPEARGS: FALLTHROUGH_INTENDED;

      case UINT8: FALLTHROUGH_INTENDED;
      case UINT16: FALLTHROUGH_INTENDED;
      case UINT32: FALLTHROUGH_INTENDED;
      case UINT64: FALLTHROUGH_INTENDED;
      case UNKNOWN_DATA:
        break;
    }
    LOG(FATAL) << "Internal error: unsupported sql type " << data_type;
    return InternalType::VALUE_NOT_SET;
  }

  static bool IsInteger(DataType t) {
    return (t >= INT8 && t <= INT64);
  }

  static bool IsNumeric(DataType t) {
    return IsInteger(t) || t == FLOAT || t == DOUBLE;
  }
  static std::string DataTypeToString(DataType type);

  // DEPRECATED: use YBSchemaBuilder instead.
  // TODO(KUDU-809): make this hard-to-use constructor private. Clients should use
  // the Builder API. Currently only the Python API uses this old API.
  YBColumnSchema(const std::string &name,
                 DataType type,
                 bool is_nullable = false,
                 bool is_hash_key = false,
                 const void* default_value = NULL,
                 YBColumnStorageAttributes attributes = YBColumnStorageAttributes());
  YBColumnSchema(const YBColumnSchema& other);
  ~YBColumnSchema();

  YBColumnSchema& operator=(const YBColumnSchema& other);

  void CopyFrom(const YBColumnSchema& other);

  bool Equals(const YBColumnSchema& other) const;

  // Getters to expose column schema information.
  const std::string& name() const;
  DataType type() const;
  bool is_hash_key() const;
  bool is_nullable() const;

  // TODO: Expose default column value and attributes?

 private:
  friend class YBColumnSpec;
  friend class YBSchema;
  friend class YBSchemaBuilder;
  // YBTableAlterer::Data needs to be a friend. Friending the parent class
  // is transitive to nested classes. See http://tiny.cloudera.com/jwtui
  friend class YBTableAlterer;

  YBColumnSchema();

  // Owned.
  ColumnSchema* col_;
};

// Builder API for specifying or altering a column within a table schema.
// This cannot be constructed directly, but rather is returned from
// YBSchemaBuilder::AddColumn() to specify a column within a Schema.
//
// TODO(KUDU-861): this API will also be used for an improved AlterTable API.
class YB_EXPORT YBColumnSpec {
 public:
  // Set the default value for this column.
  //
  // When adding a new column to a table, this default value will be used to
  // fill the new column in all existing rows.
  //
  // When a user inserts data, if the user does not specify any value for
  // this column, the default will also be used.
  //
  // The YBColumnSpec takes ownership over 'value'.
  YBColumnSpec* Default(YBValue* value);

  // Set the preferred compression for this column.
  YBColumnSpec* Compression(CompressionType compression);

  // Set the preferred encoding for this column.
  // Note that not all encodings are supported for all column types.
  YBColumnSpec* Encoding(EncodingType encoding);

  // Set the target block size for this column.
  //
  // This is the number of bytes of user data packed per block on disk, and
  // represents the unit of IO when reading this column. Larger values
  // may improve scan performance, particularly on spinning media. Smaller
  // values may improve random access performance, particularly for workloads
  // that have high cache hit rates or operate on fast storage such as SSD.
  //
  // Note that the block size specified here corresponds to uncompressed data.
  // The actual size of the unit read from disk may be smaller if
  // compression is enabled.
  //
  // It's recommended that this not be set any lower than 4096 (4KB) or higher
  // than 1048576 (1MB).
  // TODO(KUDU-1107): move above info to docs
  YBColumnSpec* BlockSize(int32_t block_size);

  // Operations only relevant for Create Table
  // ------------------------------------------------------------

  // Set this column to be the primary key of the table.
  //
  // This may only be used to set non-composite primary keys. If a composite
  // key is desired, use YBSchemaBuilder::SetPrimaryKey(). This may not be
  // used in conjunction with YBSchemaBuilder::SetPrimaryKey().
  //
  // Only relevant for a CreateTable operation. Primary keys may not be changed
  // after a table is created.
  YBColumnSpec* PrimaryKey();

  // Set this column to be a hash primary key column of the table. A hash value of all hash columns
  // in the primary key will be used to determine what partition (tablet) a particular row falls in.
  YBColumnSpec* HashPrimaryKey();

  // Set this column to be not nullable.
  // Column nullability may not be changed once a table is created.
  YBColumnSpec* NotNull();

  // Set this column to be nullable (the default).
  // Column nullability may not be changed once a table is created.
  YBColumnSpec* Nullable();

  // Set the type of this column.
  // Column types may not be changed once a table is created.
  YBColumnSpec* Type(DataType type);

  // Specify the user-defined order of the column.
  YBColumnSpec* Order(int32_t order);

  // Operations only relevant for Alter Table
  // ------------------------------------------------------------

  // Remove the default value for this column. Without a default, clients must
  // always specify a value for this column when inserting data.
  YBColumnSpec* RemoveDefault();

  // Rename this column.
  YBColumnSpec* RenameTo(const std::string& new_name);

 private:
  class YB_NO_EXPORT Data;
  friend class YBSchemaBuilder;
  friend class YBTableAlterer;

  // This class should always be owned and deleted by one of its friends,
  // not the user.
  ~YBColumnSpec();

  explicit YBColumnSpec(const std::string& col_name);

  CHECKED_STATUS ToColumnSchema(YBColumnSchema* col) const;

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
//     a float default 1.5
//   );
//
// is represented as:
//
//   YBSchemaBuilder t;
//   t.AddColumn("my_key")->Type(YBColumnSchema::INT32)->NotNull()->PrimaryKey();
//   t.AddColumn("a")->Type(YBColumnSchema::FLOAT)->Default(YBValue::FromFloat(1.5));
//   YBSchema schema;
//   t.Build(&schema);
class YB_EXPORT YBSchemaBuilder {
 public:
  YBSchemaBuilder();
  ~YBSchemaBuilder();

  // Return a YBColumnSpec for a new column within the Schema.
  // The returned object is owned by the YBSchemaBuilder.
  YBColumnSpec* AddColumn(const std::string& name);

  // Set the primary key of the new Schema based on the given column names. The first
  // 'key_hash_col_count' columns in the primary are hash columns whose values will be used for
  // table partitioning. This may be used to specify a compound primary key.
  YBSchemaBuilder* SetPrimaryKey(const std::vector<std::string>& key_col_names,
                                 int key_hash_col_count = 0);

  YBSchemaBuilder* SetTableProperties(const TableProperties& table_properties);

  // Resets 'schema' to the result of this builder.
  //
  // If the Schema is invalid for any reason (eg missing types, duplicate column names, etc)
  // a bad Status will be returned.
  CHECKED_STATUS Build(YBSchema* schema);

 private:
  class YB_NO_EXPORT Data;
  // Owned.
  Data* data_;
};

class YB_EXPORT YBSchema {
 public:
  YBSchema();

  YBSchema(const YBSchema& other);
  ~YBSchema();

  YBSchema& operator=(const YBSchema& other);
  void CopyFrom(const YBSchema& other);

  // DEPRECATED: will be removed soon.
  CHECKED_STATUS Reset(const std::vector<YBColumnSchema>& columns, int key_columns,
                       const TableProperties& table_properties) WARN_UNUSED_RESULT;

  bool Equals(const YBSchema& other) const;
  YBColumnSchema Column(size_t idx) const;
  YBColumnSchema ColumnById(int32_t id) const;

  // Returns column id provided its index.
  int32_t ColumnId(size_t idx) const;

  // Returns the number of columns in hash primary keys.
  size_t num_hash_key_columns() const;

  // Returns the number of columns in primary keys.
  size_t num_key_columns() const;

  // Returns the total number of columns.
  size_t num_columns() const;

  // Get the indexes of the primary key columns within this Schema.
  // In current versions of YB, these will always be contiguous column
  // indexes starting with 0. However, in future versions this assumption
  // may not hold, so callers should not assume it is the case.
  void GetPrimaryKeyColumnIndexes(std::vector<int>* indexes) const;

  // Create a new row corresponding to this schema.
  //
  // The new row refers to this YBSchema object, so must be destroyed before
  // the YBSchema object.
  //
  // The caller takes ownership of the created row.
  YBPartialRow* NewRow() const;

 private:
  friend class YBClient;
  friend class YBScanner;
  friend class YBSchemaBuilder;
  friend class YBTable;
  friend class YBTableCreator;
  friend class YBOperation;
  friend class YBqlReadOp;
  friend class internal::GetTableSchemaRpc;
  friend class internal::LookupRpc;
  friend class internal::WriteRpc;
  friend class yb::tools::TsAdminClient;

  friend YBSchema YBSchemaFromSchema(const Schema& schema);


  // For use by yb tests.
  explicit YBSchema(const Schema& schema);

  // Owned.
  Schema* schema_;
};

} // namespace client
} // namespace yb
#endif // YB_CLIENT_SCHEMA_H
