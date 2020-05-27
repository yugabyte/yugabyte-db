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

#include "yb/master/sys_catalog_writer.h"

#include "yb/common/ql_expr.h"
#include "yb/common/ql_protocol_util.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_ql_scanspec.h"
#include "yb/docdb/doc_rowwise_iterator.h"

#include "yb/master/sys_catalog_constants.h"

#include "yb/tablet/tablet.h"

#include "yb/util/pb_util.h"

namespace yb {
namespace master {

namespace {

void SetBinaryValue(const Slice& binary_value, QLExpressionPB* expr_pb) {
  expr_pb->mutable_value()->set_binary_value(binary_value.cdata(), binary_value.size());
}

void SetInt8Value(const int8_t int8_value, QLExpressionPB* expr_pb) {
  expr_pb->mutable_value()->set_int8_value(int8_value);
}

CHECKED_STATUS SetColumnId(
    const Schema& schema_with_ids, const std::string& column_name, QLColumnValuePB* col_pb) {
  auto column_id = VERIFY_RESULT(schema_with_ids.ColumnIdByName(column_name));
  col_pb->set_column_id(column_id.rep());
  return Status::OK();
}

} // namespace

bool IsWrite(QLWriteRequestPB::QLStmtType op_type) {
  return op_type == QLWriteRequestPB::QL_STMT_INSERT || op_type == QLWriteRequestPB::QL_STMT_UPDATE;
}

SysCatalogWriter::SysCatalogWriter(
    const std::string& tablet_id, const Schema& schema_with_ids, int64_t leader_term)
    : schema_with_ids_(schema_with_ids), leader_term_(leader_term) {
  req_.set_tablet_id(tablet_id);
}

Status SysCatalogWriter::DoMutateItem(
    int8_t type,
    const std::string& item_id,
    const google::protobuf::Message& prev_pb,
    const google::protobuf::Message& new_pb,
    QLWriteRequestPB::QLStmtType op_type) {
  const bool is_write = IsWrite(op_type);

  if (is_write) {
    std::string diff;

    if (pb_util::ArePBsEqual(prev_pb, new_pb, VLOG_IS_ON(2) ? &diff : nullptr)) {
      VLOG(2) << "Skipping empty update for item " << item_id;
      // Short-circuit empty updates.
      return Status::OK();
    }

    VLOG(2) << "Updating item " << item_id << " in catalog: " << diff;
  }

  return FillSysCatalogWriteRequest(
      type, item_id, new_pb, op_type, schema_with_ids_, req_.add_ql_write_batch());
}

Status SysCatalogWriter::InsertPgsqlTableRow(const Schema& source_schema,
                                             const QLTableRow& source_row,
                                             const TableId& target_table_id,
                                             const Schema& target_schema,
                                             const uint32_t target_schema_version,
                                             bool is_upsert) {
  PgsqlWriteRequestPB* pgsql_write = req_.add_pgsql_write_batch();

  pgsql_write->set_client(YQL_CLIENT_PGSQL);
  if (is_upsert) {
    pgsql_write->set_stmt_type(PgsqlWriteRequestPB::PGSQL_UPSERT);
  } else {
    pgsql_write->set_stmt_type(PgsqlWriteRequestPB::PGSQL_INSERT);
  }
  pgsql_write->set_table_id(target_table_id);
  pgsql_write->set_schema_version(target_schema_version);

  // Postgres sys catalog table is non-partitioned. So there should be no hash column.
  DCHECK_EQ(source_schema.num_hash_key_columns(), 0);
  for (size_t i = 0; i < source_schema.num_range_key_columns(); i++) {
    const auto& value = source_row.GetValue(source_schema.column_id(i));
    if (value) {
      pgsql_write->add_range_column_values()->mutable_value()->CopyFrom(*value);
    } else {
      return STATUS_FORMAT(Corruption, "Range value of column id $0 missing for table $1",
                           source_schema.column_id(i), target_table_id);
    }
  }
  for (size_t i = source_schema.num_range_key_columns(); i < source_schema.num_columns(); i++) {
    const auto& value = source_row.GetValue(source_schema.column_id(i));
    if (value) {
      PgsqlColumnValuePB* column_value = pgsql_write->add_column_values();
      column_value->set_column_id(target_schema.column_id(i));
      column_value->mutable_expr()->mutable_value()->CopyFrom(*value);
    }
  }

  return Status::OK();
}

Status FillSysCatalogWriteRequest(
    int8_t type, const std::string& item_id, const google::protobuf::Message& new_pb,
    QLWriteRequestPB::QLStmtType op_type, const Schema& schema_with_ids, QLWriteRequestPB* req) {
  req->set_type(op_type);

  if (IsWrite(op_type)) {
    faststring metadata_buf;

    if (!pb_util::SerializeToString(new_pb, &metadata_buf)) {
      return STATUS_FORMAT(
          Corruption, "Unable to serialize SysCatalog entry $0", item_id);
    }

    // Add the metadata column.
    QLColumnValuePB* metadata = req->add_column_values();
    RETURN_NOT_OK(SetColumnId(schema_with_ids, kSysCatalogTableColMetadata, metadata));
    SetBinaryValue(metadata_buf, metadata->mutable_expr());
  }

  // Add column type.
  SetInt8Value(type, req->add_range_column_values());

  // Add column id.
  SetBinaryValue(item_id, req->add_range_column_values());

  return Status::OK();
}

Status EnumerateSysCatalog(
    tablet::Tablet* tablet, const Schema& schema, int8_t entry_type,
    const std::function<Status(const Slice& id, const Slice& data)>& callback) {
  const int type_col_idx = VERIFY_RESULT(schema.ColumnIndexByName(kSysCatalogTableColType));
  const int entry_id_col_idx = VERIFY_RESULT(schema.ColumnIndexByName(kSysCatalogTableColId));
  const int metadata_col_idx = VERIFY_RESULT(schema.ColumnIndexByName(kSysCatalogTableColMetadata));

  auto iter = VERIFY_RESULT(tablet->NewRowIterator(
      schema.CopyWithoutColumnIds(), boost::none, ReadHybridTime::Max(), /* table_id= */"",
      CoarseTimePoint::max(), tablet::AllowBootstrappingState::kTrue));

  auto doc_iter = down_cast<docdb::DocRowwiseIterator*>(iter.get());
  QLConditionPB cond;
  cond.set_op(QL_OP_AND);
  QLAddInt8Condition(&cond, schema.column_id(type_col_idx), QL_OP_EQUAL, entry_type);
  docdb::DocQLScanSpec spec(
      schema, boost::none /* hash_code */, boost::none /* max_hash_code */,
      {} /* hashed_components */, &cond, nullptr /* if_req */, rocksdb::kDefaultQueryId);
  RETURN_NOT_OK(doc_iter->Init(spec));

  QLTableRow value_map;
  QLValue found_entry_type, entry_id, metadata;
  while (VERIFY_RESULT(iter->HasNext())) {
    RETURN_NOT_OK(iter->NextRow(&value_map));
    RETURN_NOT_OK(value_map.GetValue(schema.column_id(type_col_idx), &found_entry_type));
    SCHECK_EQ(found_entry_type.int8_value(), entry_type, Corruption, "Found wrong entry type");
    RETURN_NOT_OK(value_map.GetValue(schema.column_id(entry_id_col_idx), &entry_id));
    RETURN_NOT_OK(value_map.GetValue(schema.column_id(metadata_col_idx), &metadata));
    RETURN_NOT_OK(callback(entry_id.binary_value(), metadata.binary_value()));
  }

  return Status::OK();
}

} // namespace master
} // namespace yb
