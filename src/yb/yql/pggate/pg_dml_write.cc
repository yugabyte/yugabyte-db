//--------------------------------------------------------------------------------------------------
// Copyright (c) YugabyteDB, Inc.
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pg_dml_write.h"

#include <limits>
#include <string>
#include <utility>

#include "yb/dockv/packed_row.h"

#include "yb/gutil/casts.h"

#include "yb/util/decimal.h"
#include "yb/util/debug-util.h"

#include "catalog/pg_type_d.h"

namespace yb::pggate {

PgDmlWrite::PgDmlWrite(
    const PgSession::ScopedRefPtr& pg_session, YbcPgTransactionSetting transaction_setting,
    bool packed)
    : PgDml(pg_session), transaction_setting_(transaction_setting), packed_(packed) {
    pg_session_->SetTransactionHasWrites();
}

Status PgDmlWrite::Prepare(
    const PgObjectId& table_id, const YbcPgTableLocalityInfo& locality_info) {
  // Setup descriptors for target and bind columns.
  target_ = bind_ = PgTable(VERIFY_RESULT(pg_session_->LoadTable(table_id)));

  auto write_op = ArenaMakeShared<PgsqlWriteOp>(
      arena_ptr(), &arena(),
      /* need_transaction= */ transaction_setting_ == YbcPgTransactionSetting::YB_TRANSACTIONAL,
      locality_info);

  write_req_ = std::shared_ptr<LWPgsqlWriteRequestPB>(write_op, &write_op->write_request());
  write_req_->set_stmt_type(stmt_type());
  write_req_->set_client(YQL_CLIENT_PGSQL);
  write_req_->dup_table_id(table_id.GetYbTableId());
  write_req_->set_schema_version(target_->schema_version());
  write_req_->set_stmt_id(reinterpret_cast<uint64_t>(write_req_.get()));
  write_req_->set_metrics_capture(pg_session_->metrics().metrics_capture());

  doc_op_ = std::make_shared<PgDocWriteOp>(pg_session_, &target_, std::move(write_op));
  PrepareColumns();
  return Status::OK();
}

void PgDmlWrite::PrepareColumns() {
  // Because DocDB API requires that primary columns must be listed in their created-order,
  // the slots for primary column bind expressions are allocated here in correct order.
  for (auto& col : target_.columns()) {
    col.AllocPrimaryBindPB(write_req_.get());
  }
}

Status PgDmlWrite::DeleteEmptyPrimaryBinds() {
  if (packed()) {
    return Status::OK();
  }

  // Iterate primary-key columns and remove the binds without values.
  bool missing_primary_key = false;

  // Either ybctid or primary key must be present.
  if (!ybctid_bind_) {
    // Remove empty binds from partition list.
    size_t idx = 0;
    auto partition_iter = write_req_->mutable_partition_column_values()->begin();
    while (partition_iter != write_req_->mutable_partition_column_values()->end()) {
      if (!bind_.ColumnForIndex(idx++).ValueBound()) {
        missing_primary_key = true;
        partition_iter = write_req_->mutable_partition_column_values()->erase(partition_iter);
      } else {
        partition_iter++;
      }
    }

    // Remove empty binds from range list.
    auto range_iter = write_req_->mutable_range_column_values()->begin();
    while (range_iter != write_req_->mutable_range_column_values()->end()) {
      if (!bind_.ColumnForIndex(idx++).ValueBound()) {
        missing_primary_key = true;
        range_iter = write_req_->mutable_range_column_values()->erase(range_iter);
      } else {
        range_iter++;
      }
    }
  } else {
    write_req_->mutable_partition_column_values()->clear();
    write_req_->mutable_range_column_values()->clear();
  }

  // Check for missing key.  This is okay when binding the whole table (for colocated truncate).
  RSTATUS_DCHECK(!missing_primary_key || bind_table_, InvalidArgument,
                 "Primary key must be fully specified for modifying table");

  return Status::OK();
}

Status PgDmlWrite::Exec(ForceNonBufferable force_non_bufferable) {
  // Delete allocated binds that are not associated with a value.
  // YBClient interface enforce us to allocate binds for primary key columns in their indexing
  // order, so we have to allocate these binds before associating them with values. When the values
  // are not assigned, these allocated binds must be deleted.
  RETURN_NOT_OK(DeleteEmptyPrimaryBinds());

  // First update protobuf with new bind values.
  RETURN_NOT_OK(UpdateAssignPBs());

  // Add query comment to write request for CDC
  if (!query_comment_.empty()) {
    write_req_->dup_query_comment(query_comment_);
  }

  if (write_req_->has_ybctid_column_value()) {
    auto* exprpb = write_req_->mutable_ybctid_column_value();
    CHECK(exprpb->has_value() && exprpb->value().has_binary_value())
      << "YBCTID must be of BINARY datatype";
  }

  // Initialize doc operator.
  RETURN_NOT_OK(doc_op_->ExecuteInit(nullptr));

  // Set column references in protobuf.
  ColRefsToPB();
  // Compatibility: set column ids as expected by legacy nodes
  ColumnRefsToPB(write_req_->mutable_column_refs());

  if (targets_) {
    doc_op_->SetFetchedTargets(targets_);
  }

  // Execute the statement. If the request has been sent, get the result and handle any rows
  // returned.
  if (VERIFY_RESULT(doc_op_->Execute(ForceNonBufferable(
          force_non_bufferable.get() ||
          (transaction_setting_ == YB_SINGLE_SHARD_TRANSACTION)))) == RequestSent::kTrue) {
    RETURN_NOT_OK(doc_op_->FetchMoreResults());

    // Save the number of rows affected by the op.
    rows_affected_count_ = VERIFY_RESULT(doc_op_->GetRowsAffectedCount());
  }

  return Status::OK();
}

Status PgDmlWrite::SetWriteTime(const HybridTime& write_time) {
  SCHECK(doc_op_.get() != nullptr, RuntimeError, "expected doc_op_ to be initialized");
  down_cast<PgDocWriteOp*>(doc_op_.get())->SetWriteTime(write_time);
  return Status::OK();
}

Result<LWPgsqlExpressionPB*> PgDmlWrite::AllocColumnBindPB(PgColumn* col, PgExpr* expr) {
  return col->AllocBindPB(write_req_.get(), expr);
}

LWPgsqlExpressionPB* PgDmlWrite::AllocColumnAssignPB(PgColumn* col) {
  return col->AllocAssignPB(write_req_.get());
}

LWPgsqlExpressionPB* PgDmlWrite::AllocTargetPB() {
  return write_req_->add_targets();
}

ArenaList<LWPgsqlColRefPB>& PgDmlWrite::ColRefPBs() {
  return *write_req_->mutable_col_refs();
}

template <class T>
T DatumToYb(const YbcPgTypeEntity* type_entity, uint64_t datum) {
  T value;
  type_entity->datum_to_yb(datum, &value, nullptr);
  return value;
}

template <class T>
void PackAsUInt32(
    const YbcPgTypeEntity* type_entity, uint64_t datum, dockv::ValueEntryType type,
    ValueBuffer* out) {
  char* buf = out->GrowByAtLeast(1 + sizeof(uint32_t));
  *buf++ = static_cast<char>(type);
  BigEndian::Store32(buf, DatumToYb<T>(type_entity, datum));
}

template <class T>
void PackAsUInt64(
    const YbcPgTypeEntity* type_entity, uint64_t datum, dockv::ValueEntryType type,
    ValueBuffer* out) {
  char* buf = out->GrowByAtLeast(1 + sizeof(uint64_t));
  *buf++ = static_cast<char>(type);
  BigEndian::Store64(buf, DatumToYb<T>(type_entity, datum));
}

class PackableBindColumn final : public dockv::PackableValue {
 public:
  explicit PackableBindColumn(YbcBindColumn* column) : column_(column) {}

  void PackToV1(ValueBuffer* out) const override {
    using dockv::ValueEntryType;
    using dockv::ValueEntryTypeAsChar;

    const auto* type_entity = column_->type_entity;
    auto datum = column_->datum;

    switch (type_entity->yb_type) {
      case YB_YQL_DATA_TYPE_INT8:
        PackAsUInt32<int8_t>(type_entity, datum, ValueEntryType::kInt32, out);
        return;

      case YB_YQL_DATA_TYPE_INT16:
        PackAsUInt32<int16_t>(type_entity, datum, ValueEntryType::kInt32, out);
        return;

      case YB_YQL_DATA_TYPE_INT32:
        PackAsUInt32<int32_t>(type_entity, datum, ValueEntryType::kInt32, out);
        return;

      case YB_YQL_DATA_TYPE_UINT32:
        PackAsUInt32<uint32_t>(type_entity, datum, ValueEntryType::kUInt32, out);
        return;

      case YB_YQL_DATA_TYPE_FLOAT:
        out->push_back(ValueEntryTypeAsChar::kFloat);
        util::AppendBigEndianUInt32(
            bit_cast<uint32_t>(util::CanonicalizeFloat(DatumAsYb<float>())), out);
        return;

      case YB_YQL_DATA_TYPE_TIMESTAMP: [[fallthrough]];
      case YB_YQL_DATA_TYPE_INT64:
        PackAsUInt64<int64_t>(type_entity, datum, ValueEntryType::kInt64, out);
        return;

      case YB_YQL_DATA_TYPE_DOUBLE:
        out->push_back(ValueEntryTypeAsChar::kDouble);
        util::AppendBigEndianUInt64(
            bit_cast<uint64_t>(util::CanonicalizeDouble(DatumAsYb<double>())), out);
        return;

      case YB_YQL_DATA_TYPE_UINT64:
        PackAsUInt64<uint64_t>(type_entity, datum, ValueEntryType::kUInt64, out);
        return;

      case YB_YQL_DATA_TYPE_VECTOR: [[fallthrough]];
      case YB_YQL_DATA_TYPE_BSON: [[fallthrough]];
      case YB_YQL_DATA_TYPE_BINARY: {
        char *value;
        int64_t bytes = type_entity->datum_fixed_size;
        type_entity->datum_to_yb(datum, &value, &bytes);
        out->push_back(ValueEntryTypeAsChar::kString);
        out->append(value, bytes);
        return;
      }

      case YB_YQL_DATA_TYPE_STRING: {
        char *value;
        int64_t bytes = type_entity->datum_fixed_size;
        type_entity->datum_to_yb(column_->datum, &value, &bytes);
        if (column_->collation_info.collate_is_valid_non_c) {
          CHECK(false);
        } else {
          out->push_back(ValueEntryTypeAsChar::kString);
          out->append(value, bytes);
          return;
        }
        break;
      }

      case YB_YQL_DATA_TYPE_BOOL:
        out->push_back(
            DatumAsYb<bool>() ? ValueEntryTypeAsChar::kTrue : ValueEntryTypeAsChar::kFalse);
        return;

      case YB_YQL_DATA_TYPE_DECIMAL: {
        char* plaintext;
        type_entity->datum_to_yb(column_->datum, &plaintext, nullptr);
        util::Decimal yb_decimal(plaintext);
        out->push_back(ValueEntryTypeAsChar::kDecimal);
        out->append(yb_decimal.EncodeToComparable());
        return;
      }

      case YB_YQL_DATA_TYPE_GIN_NULL: [[fallthrough]];
      YB_PG_UNSUPPORTED_TYPES_IN_SWITCH:
      YB_PG_INVALID_TYPES_IN_SWITCH:
        break;
    }

    LOG(FATAL) << "Internal error: unsupported type " << type_entity->yb_type;
  }

  bool IsNull() const override {
    return column_->is_null;
  }

  size_t PackedSizeV1() const override {
    const auto* type_entity = column_->type_entity;

    switch (type_entity->yb_type) {
      case YB_YQL_DATA_TYPE_INT8: [[fallthrough]];
      case YB_YQL_DATA_TYPE_INT16: [[fallthrough]];
      case YB_YQL_DATA_TYPE_FLOAT: [[fallthrough]];
      case YB_YQL_DATA_TYPE_INT32: [[fallthrough]];
      case YB_YQL_DATA_TYPE_UINT32:
        return 5;

      case YB_YQL_DATA_TYPE_INT64: [[fallthrough]];
      case YB_YQL_DATA_TYPE_DOUBLE: [[fallthrough]];
      case YB_YQL_DATA_TYPE_TIMESTAMP: [[fallthrough]];
      case YB_YQL_DATA_TYPE_UINT64:
        return 9;

      case YB_YQL_DATA_TYPE_VECTOR: [[fallthrough]];
      case YB_YQL_DATA_TYPE_BSON: [[fallthrough]];
      case YB_YQL_DATA_TYPE_BINARY: {
        char *value;
        int64_t bytes = type_entity->datum_fixed_size;
        type_entity->datum_to_yb(column_->datum, &value, &bytes);
        return bytes + 1;
      }

      case YB_YQL_DATA_TYPE_STRING: {
        char *value;
        int64_t bytes = type_entity->datum_fixed_size;
        type_entity->datum_to_yb(column_->datum, &value, &bytes);
        if (column_->collation_info.collate_is_valid_non_c) {
          CHECK(false);
        } else {
          return bytes + 1;
        }
        break;
      }

      case YB_YQL_DATA_TYPE_BOOL:
        return 1;

      case YB_YQL_DATA_TYPE_DECIMAL: {
        char* plaintext;
        type_entity->datum_to_yb(column_->datum, &plaintext, nullptr);
        util::Decimal yb_decimal(plaintext);
        return yb_decimal.EncodeToComparable().size() + 1;
      }

      case YB_YQL_DATA_TYPE_GIN_NULL: [[fallthrough]];
      YB_PG_UNSUPPORTED_TYPES_IN_SWITCH:
      YB_PG_INVALID_TYPES_IN_SWITCH:
        LOG(FATAL) << "Internal error: unsupported type " << type_entity->yb_type;
    }

    return -1;
  }

  void PackToV2(ValueBuffer* out) const override {
    CHECK(false);
  }

  size_t PackedSizeV2() const override {
    CHECK(false);
    return 0;
  }

  std::string ToString() const override {
    return Format("{ type_oid: $0 yb_type: $1 }",
                  column_->type_entity->type_oid, column_->type_entity->yb_type);
  }

 private:
  template <class T>
  T DatumAsYb() const {
    return DatumToYb<T>(column_->type_entity, column_->datum);
  }

  YbcBindColumn* column_;
};

Status PgDmlWrite::BindRow(uint64_t ybctid, YbcBindColumn* columns, int count) {
  if (packed_) {
    return BindPackedRow(ybctid, columns, count);
  }
  {
    auto* expr = arena().NewObject<PgConstant>(
        &arena(), YBCPgFindTypeEntity(BYTEAOID),
        /* collate_is_valid_non_c= */ false,
        /* collation_sortkey= */ nullptr, ybctid,
        /* is_null= */ false);

    RETURN_NOT_OK(BindColumn(-8, expr));
  }

  for (auto it = columns, end = columns + count; it != end; ++it) {
    auto* expr = arena().NewObject<PgConstant>(
        &arena(), it->type_entity, it->collation_info.collate_is_valid_non_c,
        it->collation_info.sortkey, it->datum, it->is_null);
    RETURN_NOT_OK(BindColumn(it->attr_num, expr));
  }

  return Status::OK();
}

Status PgDmlWrite::BindPackedRow(uint64_t ybctid, YbcBindColumn* columns, int count) {
  {
    const auto* type_entity = YBCPgFindTypeEntity(BYTEAOID);
    uint8_t *value;
    int64_t bytes = type_entity->datum_fixed_size;
    type_entity->datum_to_yb(ybctid, &value, &bytes);
    write_req_->add_dup_packed_rows(Slice(value, bytes));
  }

  dockv::RowPackerV1 packer(
      bind_->schema_version(), bind_->schema_packing(), std::numeric_limits<ssize_t>::max(),
      Slice());
  for (auto it = columns, end = columns + count; it != end; ++it) {
    auto& column_desc = VERIFY_RESULT_REF(bind_.ColumnForAttr(it->attr_num));
    PackableBindColumn packable(it);
    auto added = VERIFY_RESULT(packer.AddValue(ColumnId(column_desc.id()), packable));
    SCHECK(added, InternalError, "Unexpectedly failed to pack column value");
  }

  write_req_->add_dup_packed_rows(VERIFY_RESULT(packer.Complete()));

  return Status::OK();
}

}  // namespace yb::pggate
