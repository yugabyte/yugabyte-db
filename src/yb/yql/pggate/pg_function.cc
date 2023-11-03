//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include <iostream>
#include <fstream>
#include <string>

#include "yb/client/yb_op.h"

#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.h"
#include "yb/common/transaction.pb.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/result.h"
#include "yb/util/status_format.h"

#include "yb/yql/pggate/pg_function.h"
#include "yb/yql/pggate/pg_function_helpers.h"
#include "yb/yql/pggate/ybc_pggate.h"
#include "yb/yql/pggate/util/pg_doc_data.h"

namespace yb {
namespace pggate {

using dockv::PgTableRow;
using dockv::PgValue;
using dockv::ReaderProjection;
using util::GetValue;
using util::SetColumnValue;
using util::SetColumnArrayValue;
//--------------------------------------------------------------------------------------------------
// PgFunctionParams
//--------------------------------------------------------------------------------------------------

Status PgFunctionParams::AddParam(
    const std::string& name, const YBCPgTypeEntity* type_entity, uint64_t datum, bool is_null) {
  auto value = std::make_shared<QLValuePB>();
  RETURN_NOT_OK(PgValueToPB(type_entity, datum, is_null, value.get()));
  params_by_name_.emplace(name, std::make_pair(value, type_entity));
  return Status::OK();
}

template <class T>
Result<ParamAndIsNullPair<T>> PgFunctionParams::GetParamValue(const std::string& col_name) const {
  const auto [value, type] = VERIFY_RESULT(GetValueAndType(col_name));
  return GetValue<T>(*value, type);
}

Result<std::pair<std::shared_ptr<const QLValuePB>, const YBCPgTypeEntity*>>
PgFunctionParams::GetValueAndType(const std::string& name) const {
  auto it = params_by_name_.find(name);
  if (it == params_by_name_.end()) {
    return STATUS_FORMAT(InvalidArgument, "Attribute name not found: $0", name);
  }
  return it->second;
}

//--------------------------------------------------------------------------------------------------
// PgFunction
//--------------------------------------------------------------------------------------------------

Status PgFunction::AddParam(
    const std::string name, const YBCPgTypeEntity* type_entity, uint64_t datum, bool is_null) {
  return params_.AddParam(name, type_entity, datum, is_null);
}

Status PgFunction::AddTarget(
    const std::string name, const YBCPgTypeEntity* type_entity, const YBCPgTypeAttrs type_attrs) {
  RETURN_NOT_OK(schema_builder_.AddColumn(name, ToLW(PersistentDataType(type_entity->yb_type))));
  RETURN_NOT_OK(schema_builder_.SetColumnPGType(name, type_entity->type_oid));
  return schema_builder_.SetColumnPGTypmod(name, type_attrs.typmod);
}

Status PgFunction::FinalizeTargets() {
  schema_ = schema_builder_.Build();
  projection_ = ReaderProjection(schema_);

  return Status::OK();
}

Status PgFunction::WritePgTuple(const PgTableRow& table_row, uint64_t* values, bool* is_nulls) {
  if (is_nulls) {
    int64_t natts = schema_.num_columns();
    memset(is_nulls, true, natts * sizeof(bool));
  }

  for (uint32_t index = 0; index < schema_.num_columns(); index++) {
    const ColumnId column_id = schema_.column_id(index);

    const std::optional<PgValue> val = table_row.GetValueByColumnId(column_id);
    if (!val) {
      continue;
    }
    const ColumnSchema column = schema_.column(index);

    uint32_t oid = column.pg_type_oid();
    const PgTypeEntity* type_entity = YBCPgFindTypeEntity(oid);
    const YBCPgTypeAttrs type_attrs = {.typmod = column.pg_typmod()};

    is_nulls[index] = false;
    RETURN_NOT_OK(PgValueToDatum(type_entity, type_attrs, *val, &values[index]));
  }

  return Status::OK();
}

Status PgFunction::GetNext(uint64_t* values, bool* is_nulls, bool* has_data) {
  if (!executed_) {
    executed_ = true;
    data_ = VERIFY_RESULT(processor_(params_, schema_, projection_, pg_session_));
    current_ = data_.begin();
  }

  if (current_ == data_.end()) {
    *has_data = false;
  } else {
    const PgTableRow& row = *current_++;

    RETURN_NOT_OK(WritePgTuple(row, values, is_nulls));
    *has_data = true;
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// PgLockStatusRequestor
//--------------------------------------------------------------------------------------------------

Result<PgTableRow> AddLock(
    const ReaderProjection& projection, const Schema& schema, const std::string& node_id,
    const TableId& parent_table_id, const std::string& tablet_id, const yb::LockInfoPB& lock,
    const TransactionId& transaction_id, HybridTime wait_start_ht = HybridTime::kMin,
    const std::vector<TransactionId>& blocking_txn_ids = {}) {
  DCHECK_NE(lock.has_wait_end_ht(), wait_start_ht != HybridTime::kMin);
  PgTableRow row(projection);
  RETURN_NOT_OK(row.SetNullOrMissingResult(schema));

  std::string locktype;
  if (lock.hash_cols_size() == 0 && lock.range_cols_size() == 0 && !lock.has_column_id()) {
    locktype = "relation";
  } else if (lock.multiple_rows_locked()) {
    locktype = "keyrange";
  } else if (lock.has_column_id()) {
    locktype = "column";
  } else {
    locktype = "row";
  }

  RETURN_NOT_OK(SetColumnValue("locktype", locktype, schema, &row));

  RSTATUS_DCHECK(
      lock.has_table_id() == parent_table_id.empty(), IllegalState,
      "Response must contain exactly one among LockInfoPB::table_id or TabletLockInfoPB::table_id");
  // If the lock belongs to a colocated table, use the table id populated in the lock.
  const auto table_id = lock.has_table_id() ? lock.table_id() : parent_table_id;
  PgOid database_oid = VERIFY_RESULT(GetPgsqlDatabaseOidByTableId(table_id));
  RETURN_NOT_OK(SetColumnValue("database", database_oid, schema, &row));

  PgOid relation_oid = VERIFY_RESULT(GetPgsqlTableOid(table_id));
  RETURN_NOT_OK(SetColumnValue("relation", relation_oid, schema, &row));

  // TODO: how to associate the pid?
  // RETURN_NOT_OK(SetColumnValue("pid", YBCGetPid(l.transaction_id()), schema, &row));

  std::vector<std::string> modes(lock.modes().size());

  std::transform(lock.modes().begin(), lock.modes().end(), modes.begin(), [](const auto& mode) {
    return LockMode_Name(static_cast<LockMode>(mode));
  });
  if (modes.size() > 0) RETURN_NOT_OK(SetColumnArrayValue("mode", modes, schema, &row));

  RETURN_NOT_OK(SetColumnValue("granted", lock.has_wait_end_ht() ? true : false, schema, &row));

  // if there is no transaction id, this is a fastpath operation
  RETURN_NOT_OK(SetColumnValue("fastpath", transaction_id.IsNil() ? true : false, schema, &row));

  if (wait_start_ht != HybridTime::kMin)
    RETURN_NOT_OK(SetColumnValue(
        "waitstart", wait_start_ht.GetPhysicalValueMicros(), schema, &row));

  if (lock.has_wait_end_ht())
    RETURN_NOT_OK(SetColumnValue(
        "waitend", HybridTime(lock.wait_end_ht()).GetPhysicalValueMicros(), schema, &row));

  if (!node_id.empty())
    RETURN_NOT_OK(SetColumnValue("node", node_id, schema, &row));
  RETURN_NOT_OK(SetColumnValue("tablet_id", tablet_id, schema, &row));

  if (!transaction_id.IsNil()) {
    RETURN_NOT_OK(SetColumnValue("transaction_id", transaction_id.GetUuid(), schema, &row));
    RETURN_NOT_OK(SetColumnValue("subtransaction_id", lock.subtransaction_id(), schema, &row));
  }

  // TODO: Add this when the RPC returns the status_tablet_id
  // RETURN_NOT_OK(SetColumnValue("status_tablet_id", lock.status_tablet_id(), schema, &row));

  RETURN_NOT_OK(SetColumnValue("is_explicit", lock.is_explicit(), schema, &row));

  if (lock.hash_cols().size() > 0)
    RETURN_NOT_OK(SetColumnArrayValue("hash_cols", lock.hash_cols(), schema, &row));
  if (lock.range_cols().size() > 0)
    RETURN_NOT_OK(SetColumnArrayValue("range_cols", lock.range_cols(), schema, &row));
  if (lock.attnum())
    RETURN_NOT_OK(SetColumnValue("attnum", lock.attnum(), schema, &row));
  if (lock.has_column_id())
    RETURN_NOT_OK(SetColumnValue("column_id", lock.column_id(), schema, &row));
  RETURN_NOT_OK(SetColumnValue("multiple_rows_locked", lock.multiple_rows_locked(), schema, &row));

  if (!blocking_txn_ids.empty())
    RETURN_NOT_OK(SetColumnArrayValue("blocked_by", blocking_txn_ids, schema, &row));

  return row;
}

Result<std::vector<TransactionId>> GetDecodedBlockerTransactionIds(
    const TabletLockInfoPB::WaiterInfoPB& waiter_info) {
  std::vector<TransactionId> decoded_blocker_txn_ids;
  decoded_blocker_txn_ids.reserve(waiter_info.blocking_txn_ids().size());
  for (const auto& blocking_txn_id : waiter_info.blocking_txn_ids()) {
    decoded_blocker_txn_ids.push_back(
        VERIFY_RESULT(FullyDecodeTransactionId(blocking_txn_id)));
  }
  return decoded_blocker_txn_ids;
}

Result<std::list<PgTableRow>> PgLockStatusRequestor(
    const PgFunctionParams& params, const Schema& schema, const ReaderProjection& projection,
    const scoped_refptr<PgSession> pg_session) {
  std::string table_id;
  const auto [relation, rel_null] = VERIFY_RESULT(params.GetParamValue<PgOid>("relation"));
  if (!rel_null) {
    const auto [database, dat_null] = VERIFY_RESULT(params.GetParamValue<PgOid>("database"));
    if (!dat_null) table_id = relation != kInvalidOid ? GetPgsqlTableId(database, relation) : "";
  }

  const auto [transaction, transaction_null] =
      VERIFY_RESULT(params.GetParamValue<Uuid>("transaction_id"));

  const auto lock_status = VERIFY_RESULT(pg_session->GetLockStatusData(
      table_id, transaction_null ? std::string() : transaction.AsSlice().ToBuffer()));

  VLOG(2) << "retrieved locks " << lock_status.DebugString();

  std::list<PgTableRow> data;

  std::unordered_map<TransactionId, std::string, TransactionIdHash> node_by_transaction;

  // Reverse the transaction map so that we can look up a node by transaction ID below
  for (const auto& [node, transaction_list] : lock_status.transactions_by_node()) {
    for (const auto& txn : transaction_list.transaction_ids()) {
      const Uuid transaction_id = VERIFY_RESULT(Uuid::FullyDecode(txn));
      auto [iterator, was_inserted] = node_by_transaction.emplace(transaction_id, node);

      // Duplicate UUIDs can occasionally occur with transaction promotion or status tablet
      // leadership changes, but this should be rare
      if (!was_inserted) {
        RSTATUS_DCHECK_EQ(
            node, iterator->second, InternalError,
            Format(
                "nodes with different UUIDs exist in the map for transaction $0 lock $1",
                transaction_id.ToString(), lock_status.DebugString()));

        VLOG(3) << "duplicate node " << node << " exists in the map for transaction "
                << transaction_id.ToString();
      }
    }
  }

  for (const auto& node : lock_status.node_locks()) {
    for (const auto& tab : node.tablet_lock_infos()) {
      for (const auto& transaction_locks : tab.transaction_locks()) {
        auto transaction_id = VERIFY_RESULT(FullyDecodeTransactionId(transaction_locks.id()));
        auto node_iter = node_by_transaction.find(transaction_id);
        // The node must exist, unless transaction_id was supplied as an argument to yb_lock_status.
        // TODO: Once issue #16913 is resolved, a node will always be returned, eliminating the need
        //       to check for transaction_null.
        DCHECK(!transaction_null || (node_iter != node_by_transaction.end()))
            << "no node found for transaction ID " << transaction_id.ToString() << " with locks "
            << lock_status.DebugString();

        const std::string node_id =
            (node_iter != node_by_transaction.end()) ? node_iter->second : "";

        for (const auto& lock : transaction_locks.granted_locks()) {
          PgTableRow row = VERIFY_RESULT(AddLock(
              projection, schema, node_id, tab.table_id(), tab.tablet_id(), lock, transaction_id));
          data.emplace_back(row);
        }

        auto wait_start_ht = HybridTime::FromPB(transaction_locks.waiting_locks().wait_start_ht());
        auto blocking_txn_ids = VERIFY_RESULT(
            GetDecodedBlockerTransactionIds(transaction_locks.waiting_locks()));
        for (const auto& lock : transaction_locks.waiting_locks().locks()) {
          PgTableRow row = VERIFY_RESULT(AddLock(
              projection, schema, node.permanent_uuid(), tab.table_id(), tab.tablet_id(), lock,
              transaction_id, wait_start_ht, blocking_txn_ids));
          data.emplace_back(row);
        }
      }

      for (const auto& waiter : tab.single_shard_waiters()) {
        auto wait_start_ht = HybridTime::FromPB(waiter.wait_start_ht());
        auto blocking_txn_ids = VERIFY_RESULT(GetDecodedBlockerTransactionIds(waiter));
        for (const auto& lock : waiter.locks()) {
          PgTableRow row = VERIFY_RESULT(AddLock(
              projection, schema, node.permanent_uuid(), tab.table_id(), tab.tablet_id(), lock,
              TransactionId::Nil(), wait_start_ht, blocking_txn_ids));
          data.emplace_back(row);
        }
      }
    }
  }

  return data;
}

}  // namespace pggate
}  // namespace yb
