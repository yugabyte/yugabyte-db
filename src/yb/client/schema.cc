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

#include <glog/logging.h>

#include "yb/client/schema-internal.h"

#include "yb/dockv/partial_row.h"
#include "yb/common/ql_type.h"
#include "yb/common/schema.h"
#include "yb/common/schema_pbutil.h"

#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/util/result.h"
#include "yb/util/status_format.h"

using std::shared_ptr;
using std::unordered_map;
using std::vector;
using std::string;

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
  data_->type = type;
  return this;
}

YBColumnSpec* YBColumnSpec::Order(int32_t order) {
  data_->order = order;
  return this;
}

YBColumnSpec* YBColumnSpec::PrimaryKey(SortingType sorting_type) {
  data_->kind = SortingTypeToColumnKind(sorting_type);
  return NotNull();
}

YBColumnSpec* YBColumnSpec::HashPrimaryKey() {
  data_->kind = ColumnKind::HASH;
  return NotNull();
}

YBColumnSpec* YBColumnSpec::StaticColumn() {
  data_->static_column = true;
  return this;
}

YBColumnSpec* YBColumnSpec::NotNull() {
  data_->nullable = Nullable::kFalse;
  return this;
}

YBColumnSpec* YBColumnSpec::Nullable() {
  data_->nullable = Nullable::kTrue;
  return this;
}

YBColumnSpec* YBColumnSpec::Counter() {
  data_->is_counter = true;
  return this;
}

YBColumnSpec* YBColumnSpec::PgTypeOid(int32_t oid) {
  data_->pg_type_oid = oid;
  return this;
}

YBColumnSpec* YBColumnSpec::RenameTo(const std::string& new_name) {
  data_->rename_to = new_name;
  return this;
}

YBColumnSpec* YBColumnSpec::SetMissing(const QLValuePB& missing_value) {
  data_->missing_value = missing_value;
  return this;
}

Status YBColumnSpec::ToColumnSchema(YBColumnSchema* col) const {
  // Verify that the user isn't trying to use any methods that
  // don't make sense for CREATE.
  if (data_->rename_to) {
    // TODO(KUDU-861): adjust these errors as this method will also be used for
    // ALTER TABLE ADD COLUMN support.
    return STATUS(NotSupported, "cannot rename a column during CreateTable",
                                data_->name);
  }

  if (!data_->type) {
    return STATUS(InvalidArgument, "no type provided for column", data_->name);
  }

  *col = YBColumnSchema(
      data_->name, data_->type, data_->kind, data_->nullable, data_->static_column,
      data_->is_counter, data_->order, data_->pg_type_oid, data_->missing_value);

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

  // Change the API to allow specifying each column individually as part of a primary key.
  // Previously, we must pass an extra list of columns if the key is a compound of columns.
  //
  // Removing the following restriction from Kudu:
  //   If they didn't explicitly pass the column names for key,
  //   then they should have set it on exactly one column.
  const YBColumnSpec::Data* reached_range_column = nullptr;
  const YBColumnSpec::Data* reached_regular_column = nullptr;
  for (size_t i = 0; i < cols.size(); i++) {
    auto& column_data = *data_->specs[i]->data_;
    if (column_data.kind == ColumnKind::HASH) {
      if (reached_range_column) {
        return STATUS_FORMAT(
            InvalidArgument, "Hash primary key column '$0' should be before primary key '$1'",
            column_data.name, reached_range_column->name);
      }
      if (reached_regular_column) {
        return STATUS_FORMAT(
            InvalidArgument, "Hash primary key column '$0' should be before regular column '$1'",
            column_data.name, reached_regular_column->name);
      }

    } else if (column_data.kind != ColumnKind::VALUE) {
      if (reached_regular_column) {
        return STATUS_FORMAT(
            InvalidArgument, "Primary key column '$0' should be before regular column '$1'",
            column_data.name, reached_regular_column->name);
      }

      reached_range_column = &column_data;
    } else {
      reached_regular_column = &column_data;
    }
  }

  if (cols.empty() || !cols.front().is_key()) {
    return STATUS(InvalidArgument, "No primary key specified");
  }

  RETURN_NOT_OK(schema->Reset(cols, data_->table_properties));
  internal::GetSchema(schema).SetSchemaName(data_->schema_name);

  return Status::OK();
}

////////////////////////////////////////////////////////////
// YBColumnSchema
////////////////////////////////////////////////////////////

YBColumnSchema::YBColumnSchema(const std::string &name,
                               const shared_ptr<QLType>& type,
                               ColumnKind kind,
                               Nullable is_nullable,
                               bool is_static,
                               bool is_counter,
                               int32_t order,
                               int32_t pg_type_oid,
                               const QLValuePB& missing_value) {
  col_ = std::make_unique<ColumnSchema>(
      name, type, kind, is_nullable, is_static, is_counter, order, pg_type_oid,
      false, missing_value);
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

bool YBColumnSchema::is_key() const {
  return DCHECK_NOTNULL(col_)->is_key();
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
    case DataType::INT8:
      return InternalType::kInt8Value;
    case DataType::INT16:
      return InternalType::kInt16Value;
    case DataType::INT32:
      return InternalType::kInt32Value;
    case DataType::INT64:
      return InternalType::kInt64Value;
    case DataType::UINT32:
      return InternalType::kUint32Value;
    case DataType::UINT64:
      return InternalType::kUint64Value;
    case DataType::FLOAT:
      return InternalType::kFloatValue;
    case DataType::DOUBLE:
      return InternalType::kDoubleValue;
    case DataType::DECIMAL:
      return InternalType::kDecimalValue;
    case DataType::STRING:
      return InternalType::kStringValue;
    case DataType::TIMESTAMP:
      return InternalType::kTimestampValue;
    case DataType::DATE:
      return InternalType::kDateValue;
    case DataType::TIME:
      return InternalType::kTimeValue;
    case DataType::INET:
      return InternalType::kInetaddressValue;
    case DataType::JSONB:
      return InternalType::kJsonbValue;
    case DataType::UUID:
      return InternalType::kUuidValue;
    case DataType::TIMEUUID:
      return InternalType::kTimeuuidValue;
    case DataType::BOOL:
      return InternalType::kBoolValue;
    case DataType::BINARY:
      return InternalType::kBinaryValue;
    case DataType::USER_DEFINED_TYPE: FALLTHROUGH_INTENDED;
    case DataType::MAP:
      return InternalType::kMapValue;
    case DataType::SET:
      return InternalType::kSetValue;
    case DataType::LIST:
      return InternalType::kListValue;
    case DataType::VARINT:
      return InternalType::kVarintValue;
    case DataType::FROZEN:
      return InternalType::kFrozenValue;
    case DataType::GIN_NULL:
      return InternalType::kGinNullValue;
    case DataType::TUPLE:
      return InternalType::kTupleValue;

    case DataType::NULL_VALUE_TYPE: FALLTHROUGH_INTENDED;
    case DataType::UNKNOWN_DATA:
      return InternalType::VALUE_NOT_SET;

    case DataType::TYPEARGS: FALLTHROUGH_INTENDED;
    case DataType::UINT8: FALLTHROUGH_INTENDED;
    case DataType::UINT16:
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

Status YBSchema::Reset(
    const vector<YBColumnSchema>& columns, const TableProperties& table_properties) {
  vector<ColumnSchema> cols_private;
  for (const YBColumnSchema& col : columns) {
    cols_private.push_back(*col.col_);
  }
  std::unique_ptr<Schema> new_schema(new Schema());
  RETURN_NOT_OK(new_schema->Reset(cols_private, table_properties));

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
  return YBColumnSchema(col.name(), col.type(), col.kind(), Nullable(col.is_nullable()),
                        col.is_static(), col.is_counter(), col.order());
}

YBColumnSchema YBSchema::ColumnById(int32_t column_id) const {
  return Column(schema_->find_column_by_id(yb::ColumnId(column_id)));
}

int32_t YBSchema::ColumnId(size_t idx) const {
  return schema_->column_id(idx);
}

std::unique_ptr<dockv::YBPartialRow> YBSchema::NewRow() const {
  return std::make_unique<dockv::YBPartialRow>(schema_.get());
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
