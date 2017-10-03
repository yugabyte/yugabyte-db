
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
#ifndef KUDU_CLIENT_SCHEMA_H
#define KUDU_CLIENT_SCHEMA_H

#include <string>
#include <vector>

#include "kudu/client/value.h"
#include "kudu/util/kudu_export.h"

namespace kudu {

class ColumnSchema;
class KuduPartialRow;
class Schema;
class TestWorkload;

namespace tools {
class TsAdminClient;
}

namespace client {

namespace internal {
class GetTableSchemaRpc;
class LookupRpc;
class WriteRpc;
} // namespace internal

class KuduClient;
class KuduSchema;
class KuduSchemaBuilder;
class KuduWriteOperation;

class KUDU_EXPORT KuduColumnStorageAttributes {
 public:
  enum EncodingType {
    AUTO_ENCODING = 0,
    PLAIN_ENCODING = 1,
    PREFIX_ENCODING = 2,
    GROUP_VARINT = 3,
    RLE = 4,
    DICT_ENCODING = 5,
    BIT_SHUFFLE = 6
  };

  enum CompressionType {
    DEFAULT_COMPRESSION = 0,
    NO_COMPRESSION = 1,
    SNAPPY = 2,
    LZ4 = 3,
    ZLIB = 4,
  };


  // NOTE: this constructor is deprecated for external use, and will
  // be made private in a future release.
  KuduColumnStorageAttributes(EncodingType encoding = AUTO_ENCODING,
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

class KUDU_EXPORT KuduColumnSchema {
 public:
  enum DataType {
    INT8 = 0,
    INT16 = 1,
    INT32 = 2,
    INT64 = 3,
    STRING = 4,
    BOOL = 5,
    FLOAT = 6,
    DOUBLE = 7,
    BINARY = 8,
    TIMESTAMP = 9
  };

  static std::string DataTypeToString(DataType type);

  // DEPRECATED: use KuduSchemaBuilder instead.
  // TODO(KUDU-809): make this hard-to-use constructor private. Clients should use
  // the Builder API. Currently only the Python API uses this old API.
  KuduColumnSchema(const std::string &name,
                   DataType type,
                   bool is_nullable = false,
                   const void* default_value = NULL,
                   KuduColumnStorageAttributes attributes = KuduColumnStorageAttributes());
  KuduColumnSchema(const KuduColumnSchema& other);
  ~KuduColumnSchema();

  KuduColumnSchema& operator=(const KuduColumnSchema& other);

  void CopyFrom(const KuduColumnSchema& other);

  bool Equals(const KuduColumnSchema& other) const;

  // Getters to expose column schema information.
  const std::string& name() const;
  DataType type() const;
  bool is_nullable() const;

  // TODO: Expose default column value and attributes?

 private:
  friend class KuduColumnSpec;
  friend class KuduSchema;
  friend class KuduSchemaBuilder;
  // KuduTableAlterer::Data needs to be a friend. Friending the parent class
  // is transitive to nested classes. See http://tiny.cloudera.com/jwtui
  friend class KuduTableAlterer;

  KuduColumnSchema();

  // Owned.
  ColumnSchema* col_;
};

// Builder API for specifying or altering a column within a table schema.
// This cannot be constructed directly, but rather is returned from
// KuduSchemaBuilder::AddColumn() to specify a column within a Schema.
//
// TODO(KUDU-861): this API will also be used for an improved AlterTable API.
class KUDU_EXPORT KuduColumnSpec {
 public:
  // Set the default value for this column.
  //
  // When adding a new column to a table, this default value will be used to
  // fill the new column in all existing rows.
  //
  // When a user inserts data, if the user does not specify any value for
  // this column, the default will also be used.
  //
  // The KuduColumnSpec takes ownership over 'value'.
  KuduColumnSpec* Default(KuduValue* value);

  // Set the preferred compression for this column.
  KuduColumnSpec* Compression(KuduColumnStorageAttributes::CompressionType compression);

  // Set the preferred encoding for this column.
  // Note that not all encodings are supported for all column types.
  KuduColumnSpec* Encoding(KuduColumnStorageAttributes::EncodingType encoding);

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
  KuduColumnSpec* BlockSize(int32_t block_size);

  // Operations only relevant for Create Table
  // ------------------------------------------------------------

  // Set this column to be the primary key of the table.
  //
  // This may only be used to set non-composite primary keys. If a composite
  // key is desired, use KuduSchemaBuilder::SetPrimaryKey(). This may not be
  // used in conjunction with KuduSchemaBuilder::SetPrimaryKey().
  //
  // Only relevant for a CreateTable operation. Primary keys may not be changed
  // after a table is created.
  KuduColumnSpec* PrimaryKey();

  // Set this column to be not nullable.
  // Column nullability may not be changed once a table is created.
  KuduColumnSpec* NotNull();

  // Set this column to be nullable (the default).
  // Column nullability may not be changed once a table is created.
  KuduColumnSpec* Nullable();

  // Set the type of this column.
  // Column types may not be changed once a table is created.
  KuduColumnSpec* Type(KuduColumnSchema::DataType type);

  // Operations only relevant for Alter Table
  // ------------------------------------------------------------

  // Remove the default value for this column. Without a default, clients must
  // always specify a value for this column when inserting data.
  KuduColumnSpec* RemoveDefault();

  // Rename this column.
  KuduColumnSpec* RenameTo(const std::string& new_name);

 private:
  class KUDU_NO_EXPORT Data;
  friend class KuduSchemaBuilder;
  friend class KuduTableAlterer;

  // This class should always be owned and deleted by one of its friends,
  // not the user.
  ~KuduColumnSpec();

  explicit KuduColumnSpec(const std::string& col_name);

  Status ToColumnSchema(KuduColumnSchema* col) const;

  // Owned.
  Data* data_;
};

// Builder API for constructing a KuduSchema object.
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
//   KuduSchemaBuilder t;
//   t.AddColumn("my_key")->Type(KuduColumnSchema::INT32)->NotNull()->PrimaryKey();
//   t.AddColumn("a")->Type(KuduColumnSchema::FLOAT)->Default(KuduValue::FromFloat(1.5));
//   KuduSchema schema;
//   t.Build(&schema);
//
class KUDU_EXPORT KuduSchemaBuilder {
 public:
  KuduSchemaBuilder();
  ~KuduSchemaBuilder();

  // Return a KuduColumnSpec for a new column within the Schema.
  // The returned object is owned by the KuduSchemaBuilder.
  KuduColumnSpec* AddColumn(const std::string& name);

  // Set the primary key of the new Schema based on the given column names.
  // This may be used to specify a compound primary key.
  KuduSchemaBuilder* SetPrimaryKey(const std::vector<std::string>& key_col_names);

  // Resets 'schema' to the result of this builder.
  //
  // If the Schema is invalid for any reason (eg missing types, duplicate column names, etc)
  // a bad Status will be returned.
  Status Build(KuduSchema* schema);

 private:
  class KUDU_NO_EXPORT Data;
  // Owned.
  Data* data_;
};

class KUDU_EXPORT KuduSchema {
 public:
  KuduSchema();

  KuduSchema(const KuduSchema& other);
  ~KuduSchema();

  KuduSchema& operator=(const KuduSchema& other);
  void CopyFrom(const KuduSchema& other);

  // DEPRECATED: will be removed soon.
  Status Reset(const std::vector<KuduColumnSchema>& columns, int key_columns)
    WARN_UNUSED_RESULT;

  bool Equals(const KuduSchema& other) const;
  KuduColumnSchema Column(size_t idx) const;
  size_t num_columns() const;

  // Get the indexes of the primary key columns within this Schema.
  // In current versions of Kudu, these will always be contiguous column
  // indexes starting with 0. However, in future versions this assumption
  // may not hold, so callers should not assume it is the case.
  void GetPrimaryKeyColumnIndexes(std::vector<int>* indexes) const;

  // Create a new row corresponding to this schema.
  //
  // The new row refers to this KuduSchema object, so must be destroyed before
  // the KuduSchema object.
  //
  // The caller takes ownership of the created row.
  KuduPartialRow* NewRow() const;

 private:
  friend class KuduClient;
  friend class KuduScanner;
  friend class KuduSchemaBuilder;
  friend class KuduTable;
  friend class KuduTableCreator;
  friend class KuduWriteOperation;
  friend class internal::GetTableSchemaRpc;
  friend class internal::LookupRpc;
  friend class internal::WriteRpc;
  friend class kudu::tools::TsAdminClient;

  friend KuduSchema KuduSchemaFromSchema(const Schema& schema);


  // For use by kudu tests.
  explicit KuduSchema(const Schema& schema);

  // Private since we don't want users to rely on the first N columns
  // being the keys.
  size_t num_key_columns() const;

  // Owned.
  Schema* schema_;
};

} // namespace client
} // namespace kudu
#endif // KUDU_CLIENT_SCHEMA_H
