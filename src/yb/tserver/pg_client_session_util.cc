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

#include "yb/tserver/pg_client_session_util.h"

#include <algorithm>
#include <array>
#include <optional>
#include <set>

#include "yb/client/batcher.h"
#include "yb/client/error.h"
#include "yb/client/schema.h"
#include "yb/client/yb_op.h"

#include "yb/common/pgsql_error.h"
#include "yb/common/pgsql_protocol.pb.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/map-util.h"

#include "yb/tserver/pg_client.messages.h"

#include "yb/util/enums.h"
#include "yb/util/status_format.h"
#include "yb/util/string_util.h"
#include "yb/util/yb_pg_errcodes.h"

namespace yb::tserver {

namespace {

std::string GetStatusStringSet(const client::CollectedErrors& errors) {
  std::set<std::string> status_strings;
  for (const auto& error : errors) {
    status_strings.insert(error->status().ToString());
  }
  return RangeToString(status_strings.begin(), status_strings.end());
}

bool IsHomogeneousErrors(const client::CollectedErrors& errors) {
  if (errors.size() < 2) {
    return true;
  }
  auto i = errors.begin();
  const auto& status = (**i).status();
  const auto codes = status.ErrorCodesSlice();
  for (++i; i != errors.end(); ++i) {
    const auto& s = (**i).status();
    if (s.code() != status.code() || codes != s.ErrorCodesSlice()) {
      return false;
    }
  }
  return true;
}

// Get a common Postgres error code from the status and all errors, and append it to a previous
// Status.
// If any of those have different conflicting error codes, previous result is returned as-is.
Status AppendPsqlErrorCode(
    const Status& status, const client::CollectedErrors& errors) {
  std::optional<YBPgErrorCode> common_psql_error;
  for(const auto& error : errors) {
    const auto psql_error = PgsqlError::ValueFromStatus(error->status());
    if (!common_psql_error) {
      common_psql_error = psql_error;
    } else if (psql_error && common_psql_error != psql_error) {
      common_psql_error.reset();
      break;
    }
  }
  return common_psql_error ? status.CloneAndAddErrorCode(PgsqlError(*common_psql_error)) : status;
}

// Get a common transaction error code for all the errors and append it to the previous Status.
Status AppendTxnErrorCode(const Status& status, const client::CollectedErrors& errors) {
  // The list of all known TransactionErrorCode (except kNone), ordered in decreasing of priority.
  static constexpr std::array precedence_list = {
      TransactionErrorCode::kDeadlock,
      TransactionErrorCode::kAborted,
      TransactionErrorCode::kConflict,
      TransactionErrorCode::kReadRestartRequired,
      TransactionErrorCode::kSnapshotTooOld,
      TransactionErrorCode::kSkipLocking,
      TransactionErrorCode::kLockNotFound};
  static_assert(precedence_list.size() + 1 == MapSize(static_cast<TransactionErrorCode*>(nullptr)));

  static const auto precedence_begin = precedence_list.begin();
  static const auto precedence_end = precedence_list.end();
  auto common_txn_error_it = precedence_end;
  for (const auto& error : errors) {
    const auto txn_error = GetTransactionErrorCode(error->status());
    if (txn_error == TransactionErrorCode::kNone ||
        (common_txn_error_it != precedence_end && *common_txn_error_it == txn_error)) {
      continue;
    }

    const auto txn_error_it = std::find(precedence_begin, precedence_end, txn_error);
    if (PREDICT_FALSE(txn_error_it == precedence_end)) {
      LOG(DFATAL) << "Unknown transaction error code: " << txn_error;
      return status;
    }

    if (txn_error_it < common_txn_error_it) {
      common_txn_error_it = txn_error_it;
      VLOG(4) << "updating common_txn_error_idx to: " << *common_txn_error_it;
    }
  }

  return common_txn_error_it == precedence_end
      ? status : status.CloneAndAddErrorCode(TransactionError(*common_txn_error_it));
}

Status ProcessUsedReadTime(uint64_t session_id,
                           const client::YBPgsqlOp& op,
                           PgPerformResponseMsg* resp,
                           TabletReadTime* used_read_time) {
  if (op.type() != client::YBOperation::PGSQL_READ) {
    return Status::OK();
  }
  const auto& read_op = down_cast<const client::YBPgsqlReadOp&>(op);
  const auto& op_used_read_time = read_op.used_read_time();
  if (!op_used_read_time) {
    return Status::OK();
  }

  if (op.table()->schema().table_properties().is_ysql_catalog_table()) {
    RSTATUS_DCHECK(
        resp != nullptr, IllegalState, "Catalog read is not expected for this caller");
    // Non empty used_read_time field means read_time for the operation has been chosen by master.
    // All further reads from catalog must use same read point. Only catalog reads riding the
    // transactional session (DDL mode, yb_non_ddl_txn_for_sys_tables_allowed) get here; legacy
    // catalog session reads always carry a read time or a clamp request, and DoPerform reports the
    // clamped time.
    auto catalog_read_time = op_used_read_time;

    // We set global limit to read time to avoid read restart errors because they are
    // disruptive to system catalog reads and it is not always possible to handle them there.
    // This might lead to reading slightly outdated state of the system catalog if a recently
    // committed DDL transaction used a transaction status tablet whose leader's clock is skewed
    // and is in the future compared to the master leader's clock.
    // TODO(dmitry) This situation will be handled in context of #7964.
    catalog_read_time.global_limit = catalog_read_time.read;
    catalog_read_time.ToPB(resp->mutable_catalog_read_time());
    VLOG(2) << "Got catalog_read_time: " << catalog_read_time.ToString();
  }

  if (used_read_time) {
    RSTATUS_DCHECK(
        !used_read_time->value, IllegalState,
        "Multiple used_read_time are not expected: $0, $1",
        used_read_time->value, op_used_read_time);
    *used_read_time = {.tablet_id = read_op.used_tablet(), .value = op_used_read_time};
  }
  return Status::OK();
}

}  // namespace

void GetTablePartitionList(const client::YBTable& table, PgTablePartitionsPB* partition_list) {
  const auto table_partition_list = table.GetVersionedPartitions();
  const auto& partition_keys = partition_list->mutable_keys();
  partition_keys->Clear();
  partition_keys->Reserve(narrow_cast<int>(table_partition_list->keys.size()));
  for (const auto& key : table_partition_list->keys) {
    *partition_keys->Add() = key;
  }
  partition_list->set_version(table_partition_list->version);
}

std::ostream& operator<<(std::ostream& str, const PrefixLogger& logger) {
  if (logger.pid_ != 0) {
    return str << "Session id " << logger.id_ << " (pid " << logger.pid_ << "): ";
  }
  return str << "Session id " << logger.id_ << ": ";
}

namespace {

Status CombineNonEmptyErrorsToStatus(
    const client::CollectedErrors& errors, const Status& status) {
  DCHECK(!errors.empty());

  if (status.IsIOError() &&
      // TODO: move away from string comparison here and use a more specific status than IOError.
      // See https://github.com/YugaByte/yugabyte-db/issues/702
      status.message() == client::internal::Batcher::kErrorReachingOutToTServersMsg &&
      IsHomogeneousErrors(errors)) {
    const auto& result = errors.front()->status();
    if (errors.size() == 1) {
      return result;
    }
    return Status(result.code(),
                  __FILE__,
                  __LINE__,
                  GetStatusStringSet(errors),
                  result.ErrorCodesSlice(),
                  /* file_name_len= */ size_t(0));
  }

  const auto result = status.ok()
      ? STATUS(InternalError, GetStatusStringSet(errors))
      : status.CloneAndAppend(". Errors from tablet servers: " + GetStatusStringSet(errors));

  return AppendTxnErrorCode(AppendPsqlErrorCode(result, errors), errors);
}

}  // namespace

Status CombineErrorsToStatus(const client::CollectedErrors& errors, const Status& status) {
  return errors.empty() ? status : CombineNonEmptyErrorsToStatus(errors, status);
}

Status HandleOperationResponse(uint64_t session_id,
                               const client::YBPgsqlOp& op,
                               PgPerformResponseMsg* resp,
                               TabletReadTime* used_read_time) {
  const auto& response = op.response();
  if (response.status() == PgsqlResponsePB::PGSQL_STATUS_OK) {
    return ProcessUsedReadTime(session_id, op, resp, used_read_time);
  }

  if (response.error_status().size() > 0) {
    // TODO(14814, 18387):  We do not currently expect more than one status, when we do, we need
    // to decide how to handle them. Possible options: aggregate multiple statuses into one, discard
    // all but one, etc. Historically, for the one set of status fields (like error_message), new
    // error message was overwriting the previous one, that's why let's return the last entry from
    // error_status to mimic that past behavior, refer AsyncRpc::Finished for details.
    return StatusFromPB(*response.error_status().rbegin());
  }

  // Older nodes may still use deprecated fields for status, so keep legacy handling
  auto status = STATUS(
      QLError, response.error_message(), Slice(), PgsqlRequestStatus(response.status()));

  if (response.has_pg_error_code()) {
    status = status.CloneAndAddErrorCode(
        PgsqlError(static_cast<YBPgErrorCode>(response.pg_error_code())));
  }

  if (response.has_txn_error_code()) {
    status = status.CloneAndAddErrorCode(
        TransactionError(static_cast<TransactionErrorCode>(response.txn_error_code())));
  }

  return status;
}

}  // namespace yb::tserver
