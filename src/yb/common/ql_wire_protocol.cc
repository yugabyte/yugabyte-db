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
#include "yb/common/ql_wire_protocol.h"

#include <string>
#include <vector>

#include "yb/common/common.pb.h"
#include "yb/common/ql_type.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.messages.h"

namespace yb {

void SchemaToColocatedTableIdentifierPB(
    const Schema& schema, ColocatedTableIdentifierPB* colocated_pb) {
  if (schema.has_colocation_id()) {
    colocated_pb->set_colocation_id(schema.colocation_id());
  } else if (schema.has_cotable_id()) {
    colocated_pb->set_cotable_id(schema.cotable_id().ToString());
  }
}

void SchemaToPB(const Schema& schema, SchemaPB *pb, int flags) {
  pb->Clear();
  SchemaToColocatedTableIdentifierPB(schema, pb->mutable_colocated_table_id());
  SchemaToColumnPBs(schema, pb->mutable_columns(), flags);
  schema.table_properties().ToTablePropertiesPB(pb->mutable_table_properties());
  pb->set_pgschema_name(schema.SchemaName());
}

void SchemaToPBWithoutIds(const Schema& schema, SchemaPB *pb) {
  pb->Clear();
  SchemaToColumnPBs(schema, pb->mutable_columns(), SCHEMA_PB_WITHOUT_IDS);
}

Status SchemaFromPB(const SchemaPB& pb, Schema *schema) {
  // Conver the columns.
  std::vector<ColumnSchema> columns;
  std::vector<ColumnId> column_ids;
  int num_key_columns = 0;
  RETURN_NOT_OK(ColumnPBsToColumnTuple(pb.columns(), &columns, &column_ids, &num_key_columns));

  // Convert the table properties.
  TableProperties table_properties = TableProperties::FromTablePropertiesPB(pb.table_properties());
  RETURN_NOT_OK(schema->Reset(columns, column_ids, num_key_columns, table_properties));

  if(pb.has_pgschema_name()) {
    schema->SetSchemaName(pb.pgschema_name());
  }

  if (pb.has_colocated_table_id()) {
    switch (pb.colocated_table_id().value_case()) {
      case ColocatedTableIdentifierPB::kCotableId: {
        schema->set_cotable_id(
            VERIFY_RESULT(Uuid::FromString(pb.colocated_table_id().cotable_id())));
        break;
      }
      case ColocatedTableIdentifierPB::kColocationId:
        schema->set_colocation_id(pb.colocated_table_id().colocation_id());
        break;
      case ColocatedTableIdentifierPB::VALUE_NOT_SET:
        break;
    }
  }
  return Status::OK();
}

void ColumnSchemaToPB(const ColumnSchema& col_schema, ColumnSchemaPB *pb, int flags) {
  pb->Clear();
  pb->set_name(col_schema.name());
  col_schema.type()->ToQLTypePB(pb->mutable_type());
  pb->set_is_nullable(col_schema.is_nullable());
  pb->set_is_static(col_schema.is_static());
  pb->set_is_counter(col_schema.is_counter());
  pb->set_order(col_schema.order());
  pb->set_sorting_type(col_schema.sorting_type());
  pb->set_pg_type_oid(col_schema.pg_type_oid());
  // We only need to process the *hash* primary key here. The regular primary key is set by the
  // conversion for SchemaPB. The reason is that ColumnSchema and ColumnSchemaPB are not matching
  // 1 to 1 as ColumnSchema doesn't have "is_key" field. That was Kudu's code, and we keep it that
  // way for now.
  if (col_schema.is_hash_key()) {
    pb->set_is_key(true);
    pb->set_is_hash_key(true);
  }
}


ColumnSchema ColumnSchemaFromPB(const ColumnSchemaPB& pb) {
  // Only "is_hash_key" is used to construct ColumnSchema. The field "is_key" will be read when
  // processing SchemaPB.
  return ColumnSchema(pb.name(), QLType::FromQLTypePB(pb.type()), pb.is_nullable(),
                      pb.is_hash_key(), pb.is_static(), pb.is_counter(), pb.order(),
                      SortingType(pb.sorting_type()), pb.pg_type_oid());
}

Status ColumnPBsToColumnTuple(
    const google::protobuf::RepeatedPtrField<ColumnSchemaPB>& column_pbs,
    std::vector<ColumnSchema>* columns , std::vector<ColumnId>* column_ids, int* num_key_columns) {
  columns->reserve(column_pbs.size());
  bool is_handling_key = true;
  for (const ColumnSchemaPB& pb : column_pbs) {
    columns->push_back(ColumnSchemaFromPB(pb));
    if (pb.is_key()) {
      if (!is_handling_key) {
        return STATUS(InvalidArgument,
                      "Got out-of-order key column", pb.ShortDebugString());
      }
      (*num_key_columns)++;
    } else {
      is_handling_key = false;
    }
    if (pb.has_id()) {
      column_ids->push_back(ColumnId(pb.id()));
    }
  }

  DCHECK_LE((*num_key_columns), columns->size());
  return Status::OK();
}

Status ColumnPBsToSchema(const google::protobuf::RepeatedPtrField<ColumnSchemaPB>& column_pbs,
                         Schema* schema) {

  std::vector<ColumnSchema> columns;
  std::vector<ColumnId> column_ids;
  int num_key_columns = 0;
  RETURN_NOT_OK(ColumnPBsToColumnTuple(column_pbs, &columns, &column_ids, &num_key_columns));

  // TODO(perf): could make the following faster by adding a
  // Reset() variant which actually takes ownership of the column
  // vector.
  return schema->Reset(columns, column_ids, num_key_columns);
}

void SchemaToColumnPBs(const Schema& schema,
                       google::protobuf::RepeatedPtrField<ColumnSchemaPB>* cols,
                       int flags) {
  cols->Clear();
  size_t idx = 0;
  for (const ColumnSchema& col : schema.columns()) {
    ColumnSchemaPB* col_pb = cols->Add();
    ColumnSchemaToPB(col, col_pb);
    col_pb->set_is_key(idx < schema.num_key_columns());

    if (schema.has_column_ids() && !(flags & SCHEMA_PB_WITHOUT_IDS)) {
      col_pb->set_id(schema.column_id(idx));
    }

    idx++;
  }
}

} // namespace yb
