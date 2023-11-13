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

#include "yb/client/schema.h"

#include <unordered_map>

#include "yb/util/logging.h"

#include "yb/client/schema-internal.h"

#include "yb/common/partial_row.h"
#include "yb/common/ql_type.h"
#include "yb/common/schema.h"
#include "yb/common/ql_wire_protocol.h"

#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/util/result.h"
#include "yb/util/status_format.h"

using std::shared_ptr;
using std::unordered_map;
using std::vector;
using std::string;
using strings::Substitute;

namespace yb {
namespace client {
////////////////////////////////////////////////////////////
// YBColumnSpec
////////////////////////////////////////////////////////////

YBColumnSpec::YBColumnSpec(const std::string& name)
  : data_(new Data(name)) {
}

YBColumnSpec::~YBColumnSpec() {
  delete data_;
}

YBColumnSpec* YBColumnSpec::Type(const std::shared_ptr<QLType>& type) {
  data_->has_type = true;
  data_->type = type;
  return this;
}

YBColumnSpec* YBColumnSpec::Order(int32_t order) {
  data_->has_order = true;
  data_->order = order;
  return this;
}

YBColumnSpec* YBColumnSpec::SetSortingType(SortingType sorting_type) {
  data_->sorting_type = sorting_type;
  return this;
}

YBColumnSpec* YBColumnSpec::PrimaryKey() {
  NotNull();
  data_->primary_key = true;
  return this;
}

YBColumnSpec* YBColumnSpec::HashPrimaryKey() {
  PrimaryKey();
  data_->hash_primary_key = true;
  return this;
}

YBColumnSpec* YBColumnSpec::StaticColumn() {
  data_->static_column = true;
  return this;
}

YBColumnSpec* YBColumnSpec::NotNull() {
  data_->has_nullable = true;
  data_->nullable = false;
  return this;
}

YBColumnSpec* YBColumnSpec::Nullable() {
  data_->has_nullable = true;
  data_->nullable = true;
  return this;
}

YBColumnSpec* YBColumnSpec::Counter() {
  data_->is_counter = true;
  return this;
}

YBColumnSpec* YBColumnSpec::PgTypeOid(int32_t oid) {
  data_->has_pg_type_oid = true;
  data_->pg_type_oid = oid;
  return this;
}

YBColumnSpec* YBColumnSpec::RenameTo(const std::string& new_name) {
  data_->has_rename_to = true;
  data_->rename_to = new_name;
  return this;
}

Status YBColumnSpec::ToColumnSchema(YBColumnSchema* col) const {
  // Verify that the user isn't trying to use any methods that
  // don't make sense for CREATE.
  if (data_->has_rename_to) {
    // TODO(KUDU-861): adjust these errors as this method will also be used for
    // ALTER TABLE ADD COLUMN support.
    return STATUS(NotSupported, "cannot rename a column during CreateTable",
                                data_->name);
  }

  if (!data_->has_type) {
    return STATUS(InvalidArgument, "no type provided for column", data_->name);
  }

  bool nullable = data_->has_nullable ? data_->nullable : true;

  *col = YBColumnSchema(data_->name, data_->type, nullable, data_->hash_primary_key,
                        data_->static_column, data_->is_counter, data_->order,
                        data_->sorting_type, data_->pg_type_oid);

  return Status::OK();
}

YBColumnSpec* YBColumnSpec::Type(DataType type) {
  return Type(QLType::Create(type));
}

////////////////////////////////////////////////////////////
// YBSchemaBuilder
////////////////////////////////////////////////////////////

class YBSchemaBuilder::Data {
 public:
  ~Data() {
    // Rather than delete the specs here, we have to do it in
    // ~YBSchemaBuilder(), to avoid a circular dependency in the
    // headers declaring friend classes with nested classes.
  }

  // These members can be used to specify a subset of columns are primary or hash primary keys.
  // NOTE: "key_col_names" and "key_hash_col_count" are not used unless "has_key_col_names" is true.
  bool has_key_col_names = false;
  vector<string> key_col_names;
  size_t key_hash_col_count = 0;

  vector<YBColumnSpec*> specs;
  TableProperties table_properties;
  std::string schema_name;
};

YBSchemaBuilder::YBSchemaBuilder()
  : data_(new Data()) {
}

YBSchemaBuilder::~YBSchemaBuilder() {
  for (YBColumnSpec* spec : data_->specs) {
    // Can't use STLDeleteElements because YBSchemaBuilder
    // is a friend of YBColumnSpec in order to access its destructor.
    // STLDeleteElements is a free function and therefore can't access it.
    delete spec;
  }
  delete data_;
}

YBColumnSpec* YBSchemaBuilder::AddColumn(const std::string& name) {
  auto c = new YBColumnSpec(name);
  data_->specs.push_back(c);
  return c;
}

YBSchemaBuilder* YBSchemaBuilder::SetPrimaryKey(
    const std::vector<std::string>& key_col_names,
    size_t key_hash_col_count) {
  data_->has_key_col_names = true;
  data_->key_col_names = key_col_names;
  data_->key_hash_col_count = key_hash_col_count;
  return this;
}

YBSchemaBuilder* YBSchemaBuilder::SetTableProperties(const TableProperties& table_properties) {
  data_->table_properties = table_properties;
  return this;
}

YBSchemaBuilder* YBSchemaBuilder::SetSchemaName(const std::string& pgschema_name) {
  data_->schema_name = pgschema_name;
  return this;
}

std::string YBSchemaBuilder::SchemaName() {
  return data_->schema_name;
}

Status YBSchemaBuilder::Build(YBSchema* schema) {
  std::vector<YBColumnSchema> cols(data_->specs.size(), YBColumnSchema());
  for (size_t i = 0; i < cols.size(); i++) {
    RETURN_NOT_OK(data_->specs[i]->ToColumnSchema(&cols[i]));
  }

  size_t num_key_cols = 0;
  if (!data_->has_key_col_names) {
    // Change the API to allow specifying each column individually as part of a primary key.
    // Previously, we must pass an extra list of columns if the key is a compound of columns.
    //
    // Removing the following restriction from Kudu:
    //   If they didn't explicitly pass the column names for key,
    //   then they should have set it on exactly one column.
    const YBColumnSpec::Data* reached_primary_column = nullptr;
    const YBColumnSpec::Data* reached_regular_column = nullptr;
    for (size_t i = 0; i < cols.size(); i++) {
      auto& column_data = *data_->specs[i]->data_;
      if (column_data.hash_primary_key) {
        num_key_cols++;
        if (reached_primary_column) {
          return STATUS_FORMAT(
              InvalidArgument, "Hash primary key column '$0' should be before primary key '$1'",
              column_data.name, reached_primary_column->name);
        }
        if (reached_regular_column) {
          return STATUS_FORMAT(
              InvalidArgument, "Hash primary key column '$0' should be before regular column '$1'",
              column_data.name, reached_regular_column->name);
        }

      } else if (column_data.primary_key) {
        num_key_cols++;
        if (reached_regular_column) {
          return STATUS_FORMAT(
              InvalidArgument, "Primary key column '$0' should be before regular column '$1'",
              column_data.name, reached_regular_column->name);
        }

        reached_primary_column = &column_data;
      } else {
        reached_regular_column = &column_data;
      }
    }

    if (num_key_cols <= 0) {
      return STATUS(InvalidArgument, "No primary key specified");
    }
  } else {
    // Build a map from name to index of all of the columns.
    unordered_map<string, size_t> name_to_idx_map;
    size_t i = 0;
    for (YBColumnSpec* spec : data_->specs) {
      // If they did pass the key column names, then we should not have explicitly
      // set it on any columns.
      if (spec->data_->primary_key) {
        return STATUS(InvalidArgument, "Primary key specified by both SetPrimaryKey() and on a "
                                       "specific column", spec->data_->name);
      }

      // Set the primary keys here to make sure the two different APIs for ColumnSpecs yield the
      // same result.
      if (i < data_->key_hash_col_count) {
        spec->HashPrimaryKey();
      } else {
        spec->PrimaryKey();
      }

      // If we have a duplicate column name, the Schema::Reset() will catch it later,
      // anyway.
      name_to_idx_map[spec->data_->name] = i++;
    }

    // Convert the key column names to a set of indexes.
    vector<size_t> key_col_indexes;
    for (const string& key_col_name : data_->key_col_names) {
      size_t idx;
      if (!FindCopy(name_to_idx_map, key_col_name, &idx)) {
        return STATUS(InvalidArgument, "Primary key column not defined", key_col_name);
      }
      key_col_indexes.push_back(idx);
    }

    // Currently we require that the key columns be contiguous at the front
    // of the schema. We'll lift this restriction later -- hence the more
    // flexible user-facing API.
    for (size_t i = 0; i < key_col_indexes.size(); i++) {
      if (key_col_indexes[i] != i) {
        return STATUS(InvalidArgument, "Primary key columns must be listed first in the schema",
                                       data_->key_col_names[i]);
      }
    }

    // Indicate the first "num_key_cols" are primary key.
    num_key_cols = key_col_indexes.size();
  }

  RETURN_NOT_OK(schema->Reset(cols, num_key_cols, data_->table_properties));
  internal::GetSchema(schema).SetSchemaName(data_->schema_name);

  return Status::OK();
}

////////////////////////////////////////////////////////////
// YBColumnSchema
////////////////////////////////////////////////////////////

YBColumnSchema::YBColumnSchema(const std::string &name,
                               const shared_ptr<QLType>& type,
                               bool is_nullable,
                               bool is_hash_key,
                               bool is_static,
                               bool is_counter,
                               int32_t order,
                               SortingType sorting_type,
                               int32_t pg_type_oid) {
  col_ = std::make_unique<ColumnSchema>(name, type, is_nullable, is_hash_key, is_static, is_counter,
                                        order, sorting_type, pg_type_oid);
}

YBColumnSchema::YBColumnSchema(const YBColumnSchema& other) {
  CopyFrom(other);
}

YBColumnSchema::YBColumnSchema() = default;

YBColumnSchema::~YBColumnSchema() = default;

YBColumnSchema& YBColumnSchema::operator=(const YBColumnSchema& other) {
  if (&other != this) {
    CopyFrom(other);
  }
  return *this;
}

void YBColumnSchema::CopyFrom(const YBColumnSchema& other) {
  col_.reset();
  if (other.col_) {
    col_ = std::make_unique<ColumnSchema>(*other.col_);
  }
}

bool YBColumnSchema::Equals(const YBColumnSchema& other) const {
  return this == &other || col_ == other.col_ || (col_ != nullptr && col_->Equals(*other.col_));
}

const std::string& YBColumnSchema::name() const {
  return DCHECK_NOTNULL(col_)->name();
}

bool YBColumnSchema::is_nullable() const {
  return DCHECK_NOTNULL(col_)->is_nullable();
}

bool YBColumnSchema::is_hash_key() const {
  return DCHECK_NOTNULL(col_)->is_hash_key();
}

bool YBColumnSchema::is_static() const {
  return DCHECK_NOTNULL(col_)->is_static();
}

const shared_ptr<QLType>& YBColumnSchema::type() const {
  return DCHECK_NOTNULL(col_)->type();
}

SortingType YBColumnSchema::sorting_type() const {
  return DCHECK_NOTNULL(col_)->sorting_type();
}

bool YBColumnSchema::is_counter() const {
  return DCHECK_NOTNULL(col_)->is_counter();
}

int32_t YBColumnSchema::order() const {
  return DCHECK_NOTNULL(col_)->order();
}

int32_t YBColumnSchema::pg_type_oid() const {
  return DCHECK_NOTNULL(col_)->pg_type_oid();
}

InternalType YBColumnSchema::ToInternalDataType(DataType type) {
  switch (type) {
    case INT8:
      return InternalType::kInt8Value;
    case INT16:
      return InternalType::kInt16Value;
    case INT32:
      return InternalType::kInt32Value;
    case INT64:
      return InternalType::kInt64Value;
    case UINT32:
      return InternalType::kUint32Value;
    case UINT64:
      return InternalType::kUint64Value;
    case FLOAT:
      return InternalType::kFloatValue;
    case DOUBLE:
      return InternalType::kDoubleValue;
    case DECIMAL:
      return InternalType::kDecimalValue;
    case STRING:
      return InternalType::kStringValue;
    case TIMESTAMP:
      return InternalType::kTimestampValue;
    case DATE:
      return InternalType::kDateValue;
    case TIME:
      return InternalType::kTimeValue;
    case INET:
      return InternalType::kInetaddressValue;
    case JSONB:
      return InternalType::kJsonbValue;
    case UUID:
      return InternalType::kUuidValue;
    case TIMEUUID:
      return InternalType::kTimeuuidValue;
    case BOOL:
      return InternalType::kBoolValue;
    case BINARY:
      return InternalType::kBinaryValue;
    case USER_DEFINED_TYPE: FALLTHROUGH_INTENDED;
    case MAP:
      return InternalType::kMapValue;
    case SET:
      return InternalType::kSetValue;
    case LIST:
      return InternalType::kListValue;
    case VARINT:
      return InternalType::kVarintValue;
    case FROZEN:
      return InternalType::kFrozenValue;
    case GIN_NULL:
      return InternalType::kGinNullValue;
    case TUPLE:
      return InternalType::kTupleValue;

    case NULL_VALUE_TYPE: FALLTHROUGH_INTENDED;
    case UNKNOWN_DATA:
      return InternalType::VALUE_NOT_SET;

    case TYPEARGS: FALLTHROUGH_INTENDED;
    case UINT8: FALLTHROUGH_INTENDED;
    case UINT16:
      break;
  }
  LOG(FATAL) << "Internal error: unsupported type " << type;
  return InternalType::VALUE_NOT_SET;
}

InternalType YBColumnSchema::ToInternalDataType(const std::shared_ptr<QLType>& ql_type) {
  return ToInternalDataType(ql_type->main());
}

////////////////////////////////////////////////////////////
// YBSchema
////////////////////////////////////////////////////////////

namespace internal {

const Schema& GetSchema(const YBSchema& schema) {
  return *schema.schema_;
}

Schema& GetSchema(YBSchema* schema) {
  return *schema->schema_;
}

} // namespace internal

YBSchema::YBSchema() {}

YBSchema::YBSchema(const YBSchema& other) {
  CopyFrom(other);
}

YBSchema::YBSchema(YBSchema&& other) {
  MoveFrom(std::move(other));
}

YBSchema::YBSchema(const Schema& schema)
    : schema_(new Schema(schema)) {
}

YBSchema::~YBSchema() {
}

YBSchema& YBSchema::operator=(const YBSchema& other) {
  if (&other != this) {
    CopyFrom(other);
  }
  return *this;
}

YBSchema& YBSchema::operator=(YBSchema&& other) {
  if (&other != this) {
    MoveFrom(std::move(other));
  }
  return *this;
}

void YBSchema::CopyFrom(const YBSchema& other) {
  schema_.reset(new Schema(*other.schema_));
  version_ = other.version();
  is_compatible_with_previous_version_ = other.is_compatible_with_previous_version();
}

void YBSchema::MoveFrom(YBSchema&& other) {
  schema_ = std::move(other.schema_);
  version_ = other.version();
  is_compatible_with_previous_version_ = other.is_compatible_with_previous_version();
}

void YBSchema::Reset(std::unique_ptr<Schema> schema) {
  schema_ = std::move(schema);
}

Status YBSchema::Reset(const vector<YBColumnSchema>& columns, size_t key_columns,
                       const TableProperties& table_properties) {
  vector<ColumnSchema> cols_private;
  for (const YBColumnSchema& col : columns) {
    cols_private.push_back(*col.col_);
  }
  std::unique_ptr<Schema> new_schema(new Schema());
  RETURN_NOT_OK(new_schema->Reset(cols_private, key_columns, table_properties));

  schema_ = std::move(new_schema);
  return Status::OK();
}

bool YBSchema::Equals(const YBSchema& other) const {
  return this == &other ||
         (schema_.get() && other.schema_.get() && schema_->Equals(*other.schema_));
}

bool YBSchema::EquivalentForDataCopy(const YBSchema& source) const {
  return this == &source ||
      (schema_.get() && source.schema_.get() && schema_->EquivalentForDataCopy(*source.schema_));
}

Result<bool> YBSchema::Equals(const SchemaPB& other) const {
  Schema schema;
  RETURN_NOT_OK(SchemaFromPB(other, &schema));

  YBSchema yb_schema(schema);
  return Equals(yb_schema);
}

Result<bool> YBSchema::EquivalentForDataCopy(const SchemaPB& source) const {
  Schema source_schema;
  RETURN_NOT_OK(SchemaFromPB(source, &source_schema));

  YBSchema source_yb_schema(source_schema);
  return EquivalentForDataCopy(source_yb_schema);
}

const TableProperties& YBSchema::table_properties() const {
  return schema_->table_properties();
}

YBColumnSchema YBSchema::Column(size_t idx) const {
  ColumnSchema col(schema_->column(idx));
  return YBColumnSchema(col.name(), col.type(), col.is_nullable(), col.is_hash_key(),
                        col.is_static(), col.is_counter(), col.order(), col.sorting_type());
}

YBColumnSchema YBSchema::ColumnById(int32_t column_id) const {
  return Column(schema_->find_column_by_id(yb::ColumnId(column_id)));
}

int32_t YBSchema::ColumnId(size_t idx) const {
  return schema_->column_id(idx);
}

std::unique_ptr<YBPartialRow> YBSchema::NewRow() const {
  return std::make_unique<YBPartialRow>(schema_.get());
}

const std::vector<ColumnSchema>& YBSchema::columns() const {
  return schema_->columns();
}

size_t YBSchema::num_columns() const {
  return schema_->num_columns();
}

size_t YBSchema::num_key_columns() const {
  return schema_->num_key_columns();
}

size_t YBSchema::num_hash_key_columns() const {
  return schema_->num_hash_key_columns();
}

size_t YBSchema::num_range_key_columns() const {
  return schema_->num_range_key_columns();
}

bool YBSchema::has_colocation_id() const {
  return schema_->has_colocation_id();
}

ColocationId YBSchema::colocation_id() const {
  return schema_->colocation_id();
}

bool YBSchema::is_compatible_with_previous_version() const {
  return is_compatible_with_previous_version_;
}

void YBSchema::set_is_compatible_with_previous_version(bool is_compatible) {
  is_compatible_with_previous_version_ = is_compatible;
}

uint32_t YBSchema::version() const {
  return version_;
}

void YBSchema::set_version(uint32_t version) {
  version_ = version;
}

std::vector<size_t> YBSchema::GetPrimaryKeyColumnIndexes() const {
  std::vector<size_t> result(num_key_columns());
  for (size_t i = 0; i < num_key_columns(); i++) {
    result[i] = i;
  }
  return result;
}

string YBSchema::ToString() const {
  return schema_->ToString();
}

ssize_t YBSchema::FindColumn(const GStringPiece& name) const {
  return schema_->find_column(name);
}

} // namespace client
} // namespace yb
