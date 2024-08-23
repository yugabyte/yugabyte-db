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

#include "yb/master/ysql_sequence_util.h"

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/yb_op.h"

#include "yb/common/entity_ids.h"
#include "yb/common/pgsql_protocol.pb.h"

#include "yb/rpc/sidecars.h"

#include "yb/tserver/service_util.h"

#include "yb/util/status.h"
#include "yb/util/status_format.h"

#include "yb/yql/pggate/util/pg_doc_data.h"

DECLARE_int32(master_yb_client_default_timeout_ms);

namespace yb::master {

namespace {

constexpr const size_t kPgSequenceLastValueColIdx = 2;
constexpr const size_t kPgSequenceIsCalledColIdx = 3;

Result<client::YBTablePtr> OpenSequencesDataTable(client::YBClient& client) {
  PgObjectId table_oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
  return client.OpenTable(table_oid.GetYbTableId());
}

// Read next value from a sidebar, assuming it to be a non-null number of type T.
template <typename T>
Result<T> ReadNonNullNumber(Slice* cursor_into_sidebar) {
  bool value_is_null = pggate::PgDocData::ReadHeaderIsNull(cursor_into_sidebar);
  SCHECK(!value_is_null, Corruption, "Unexpected NULL value while reading table row");
  return pggate::PgDocData::ReadNumber<T>(cursor_into_sidebar);
}

}  // anonymous namespace

Result<std::vector<YsqlSequenceInfo>> ScanSequencesDataTable(
    client::YBClient& client, uint32_t db_oid, uint64_t max_rows_per_read, bool TEST_fail_read) {
  auto table = VERIFY_RESULT(OpenSequencesDataTable(client));
  RSTATUS_DCHECK_EQ(
      table->schema().num_columns(), 4, IllegalState,
      "sequences_data DocDB table has wrong number of columns");

  auto session =
      client.NewSession(MonoDelta::FromMilliseconds(FLAGS_master_yb_client_default_timeout_ms));
  session->set_allow_local_calls_in_curr_thread(false);

  PgsqlPagingStatePB* paging_state = nullptr;
  std::vector<YsqlSequenceInfo> results;
  do {
    // Prepare a YBPgsqlReadOp to read all the rows of the sequences_data table.
    //
    // It doesn't appear possible to just attach a WHERE condition here to select only the rows for
    // the db_oid database: where_expr isn't supported and where_clauses require serialized Postgres
    // expressions.  Accordingly, we will fetch everything then filter the results.
    rpc::Sidecars sidecars;
    auto psql_read = client::YBPgsqlReadOp::NewSelect(table, &sidecars);
    auto read_request = psql_read->mutable_request();
    // Each row has four columns and we want all of them.
    for (int i = 0; i < 4; i++) {
      read_request->add_targets()->set_column_id(table->schema().ColumnId(i));
      read_request->mutable_column_refs()->add_ids(table->schema().ColumnId(i));
      read_request->add_col_refs()->set_column_id(table->schema().ColumnId(i));
    }
    if (TEST_fail_read) {
      // If requested by a test, we add a reference to a nonexistent column; this causes the read to
      // fail in interesting way that does not cause TEST_ApplyAndFlush to fail.  This exercises the
      // error handling code after TEST_ApplyAndFlush.
      read_request->add_col_refs()->set_column_id(66666);
    }
    // We do not set the PG catalog version numbers in the request because we don't care what the PG
    // catalog state is.
    read_request->set_limit(max_rows_per_read);
    read_request->set_return_paging_state(true);
    if (paging_state) {
      read_request->set_allocated_paging_state(paging_state);
      paging_state = nullptr;
    }
    VLOG(3) << "read request: " << read_request->DebugString();

    // Execute the operation synchronously.  Some but not all ways psql_read fails will cause
    // TEST_ApplyAndFlush to fail.
    // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
    RETURN_NOT_OK(session->TEST_ApplyAndFlush(psql_read));
    const auto& response = psql_read.get()->response();
    VLOG(3) << "read response: " << response.DebugString();
    if (response.status() != PgsqlResponsePB::PGSQL_STATUS_OK) {
      if (response.error_status().size() > 0) {
        // TODO(14814, 18387):  We do not currently expect more than one status, when we do, we need
        // to decide how to handle them.  Possible options: aggregate multiple statuses into one,
        // discard all but one, etc.  Historically, for the one set of status fields (like
        // error_message), new error message was overwriting the previous one, that's why let's
        // return the last entry from error_status to mimic that past behavior, refer
        // AsyncRpc::Finished for details.
        return StatusFromPB(*response.error_status().rbegin());
      } else {
        // This should not happen because other ways of returning errors are deprecated.
        return STATUS_FORMAT(
            InternalError, "Unknown error while trying to read from sequences_data DocDB table: $0",
            PgsqlResponsePB::RequestStatus_Name(response.status()));
      }
    }

    // Extract the rows from the returned sidecar.
    Slice cursor;
    int64_t row_count = 0;
    pggate::PgDocData::LoadCache(sidecars.GetFirst(), &row_count, &cursor);
    for (int i = 0; i < row_count; i++) {
      YsqlSequenceInfo info;
      auto read_db_oid = VERIFY_RESULT(ReadNonNullNumber<int64_t>(&cursor));
      info.sequence_oid = VERIFY_RESULT(ReadNonNullNumber<int64_t>(&cursor));
      info.last_value = VERIFY_RESULT(ReadNonNullNumber<int64_t>(&cursor));
      info.is_called = VERIFY_RESULT(ReadNonNullNumber<bool>(&cursor));
      if (read_db_oid == db_oid) {
        results.push_back(info);
      }
    }

    auto* resp = psql_read->mutable_response();
    if (resp->has_paging_state()) {
      paging_state = resp->release_paging_state();
    }
  } while (paging_state != nullptr);

  return results;
}

Result<int> EnsureSequenceUpdatesInWal(
    client::YBClient& client, uint32_t db_oid, const std::vector<YsqlSequenceInfo>& sequences) {
  auto table = VERIFY_RESULT(OpenSequencesDataTable(client));

  auto session =
      client.NewSession(MonoDelta::FromMilliseconds(FLAGS_master_yb_client_default_timeout_ms));
  session->set_allow_local_calls_in_curr_thread(false);

  std::vector<rpc::Sidecars> sidecars_storage{sequences.size()};
  std::vector<client::YBPgsqlWriteOpPtr> operations;
  for (size_t i = 0; i < sequences.size(); i++) {
    const auto& sequence = sequences[i];

    // The following code creates a new YBPgsqlWriteOp operation equivalent to:
    //
    // UPDATE sequences_data SET last_value={sequence.last_value}, is_called={sequence.is_called}
    //   WHERE db_oid={db_oid} AND seq_oid={sequence.sequence_oid}
    //   AND last_value={sequence.last_value} AND is_called={sequence.is_called}

    auto psql_write = client::YBPgsqlWriteOp::NewUpdate(table, &sidecars_storage[i]);
    auto write_request = psql_write->mutable_request();
    // We do not set the PG catalog version numbers in the request because we don't care what the PG
    // catalog state is.

    // Set primary key to db_oid, sequence.sequence_oid.
    write_request->add_partition_column_values()->mutable_value()->set_int64_value(db_oid);
    write_request->add_partition_column_values()->mutable_value()->set_int64_value(
        sequence.sequence_oid);

    // The SET part.
    PgsqlColumnValuePB* column_value = write_request->add_column_new_values();
    column_value->set_column_id(table->schema().ColumnId(kPgSequenceLastValueColIdx));
    column_value->mutable_expr()->mutable_value()->set_int64_value(sequence.last_value);
    column_value = write_request->add_column_new_values();
    column_value->set_column_id(table->schema().ColumnId(kPgSequenceIsCalledColIdx));
    column_value->mutable_expr()->mutable_value()->set_bool_value(sequence.is_called);

    // The WHERE part without the primary key.
    auto where_pb = write_request->mutable_where_expr()->mutable_condition();
    where_pb->set_op(QL_OP_AND);
    auto cond = where_pb->add_operands()->mutable_condition();
    cond->set_op(QL_OP_EQUAL);
    cond->add_operands()->set_column_id(table->schema().ColumnId(kPgSequenceLastValueColIdx));
    cond->add_operands()->mutable_value()->set_int64_value(sequence.last_value);
    cond = where_pb->add_operands()->mutable_condition();
    cond->set_op(QL_OP_EQUAL);
    cond->add_operands()->set_column_id(table->schema().ColumnId(kPgSequenceIsCalledColIdx));
    cond->add_operands()->mutable_value()->set_bool_value(sequence.is_called);

    // For compatibility set deprecated column_refs
    write_request->mutable_column_refs()->add_ids(
        table->schema().ColumnId(kPgSequenceLastValueColIdx));
    write_request->mutable_column_refs()->add_ids(
        table->schema().ColumnId(kPgSequenceIsCalledColIdx));
    // Same values, to be consumed by current TServers
    write_request->add_col_refs()->set_column_id(
        table->schema().ColumnId(kPgSequenceLastValueColIdx));
    write_request->add_col_refs()->set_column_id(
        table->schema().ColumnId(kPgSequenceIsCalledColIdx));

    // Add the operation to the operations the session should do once a flush is submitted.
    VLOG(3) << "write request: " << write_request->DebugString();
    operations.push_back(psql_write);
    session->Apply(std::move(psql_write));
  }

  // Synchronously execute the operations.
  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  RETURN_NOT_OK(session->TEST_Flush());

  int updates = 0;
  for (const auto& operation : operations) {
    const auto& response = operation.get()->response();
    VLOG(3) << "write response: " << response.DebugString();

    if (!operation->response().skipped()) {
      updates++;
    }

    if (response.status() != PgsqlResponsePB::PGSQL_STATUS_OK) {
      if (response.error_status().size() > 0) {
        // TODO(14814, 18387):  We do not currently expect more than one status, when we do, we need
        // to decide how to handle them.  Possible options: aggregate multiple statuses into one,
        // discard all but one, etc.  Historically, for the one set of status fields (like
        // error_message), new error message was overwriting the previous one, that's why let's
        // return the last entry from error_status to mimic that past behavior, refer
        // AsyncRpc::Finished for details.
        return StatusFromPB(*response.error_status().rbegin());
      } else {
        // This should not happen because other ways of returning errors are deprecated.
        return STATUS_FORMAT(
            InternalError, "Unknown error while trying to update sequences_data DocDB table: $0",
            PgsqlResponsePB::RequestStatus_Name(response.status()));
      }
    }
  }
  return updates;
}
}  // namespace yb::master
