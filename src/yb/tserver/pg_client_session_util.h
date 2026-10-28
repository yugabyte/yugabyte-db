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

// Utilities shared by PgClientService and ThinClientService session code: read point history for
// restoring a snapshot by read_time_serial_no, and helpers to convert pgsql op results into a
// status.

#pragma once

#include <sys/types.h>

#include <string>
#include <unordered_map>

#include "yb/client/client_fwd.h"
#include "yb/client/table.h"

#include "yb/common/consistent_read_point.h"
#include "yb/common/entity_ids_types.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/transaction_error.h"
#include "yb/common/wire_protocol.h"

#include "yb/master/master_ddl.pb.h"

#include "yb/rpc/rpc_context.h"

#include "yb/tserver/pg_client.fwd.h"
#include "yb/tserver/pg_table_cache.h"
#include "yb/tserver/tserver_fwd.h"

#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/tostring.h"

namespace yb::tserver {

template <class Resp>
void Respond(const Status& status, Resp* resp, rpc::RpcContext* context) {
  if (!status.ok()) {
    if constexpr (HasMemberFunction_status<Resp>::value) {
      StatusToPB(status, resp->mutable_status());
    } else {
      auto* error = resp->mutable_error();
      StatusToPB(status, error->mutable_status());
      error->set_code(error->code());
    }
  }
  context->RespondSuccess();
}

void GetTablePartitionList(const client::YBTable& table, PgTablePartitionsPB* partition_list);

// Serves an OpenTable-style RPC from the table cache lookup, filling the schema info of the
// requested table. A subclass may add response fields via Fill.
template <class Req, class Resp>
class OpenTableQueryBase : public PgTablesQueryListener {
 public:
  using ContextHolder = rpc::TypedPBRpcContextHolder<Req, Resp>;

  explicit OpenTableQueryBase(ContextHolder&& context) : context_(std::move(context)) {}

  void Ready(const PgTablesQueryResult& tables) override {
    auto res = tables.GetInfo(context_.req().table_id());
    auto& resp = context_.resp();
    if (!res.ok()) {
      Respond(res.status(), &resp, &context_.context());
      return;
    }
    *resp.mutable_info() = *res->schema;
    Fill(*res, &resp);
    context_->RespondSuccess();
  }

 protected:
  virtual void Fill(const PgTablesQueryResult::TableInfo& info, Resp* resp) {}

 private:
  ContextHolder context_;
};

class PrefixLogger {
 public:
  explicit PrefixLogger(uint64_t id, pid_t pid = 0) : id_(id), pid_(pid) {}

  friend std::ostream& operator<<(std::ostream&, const PrefixLogger&);

 private:
  const uint64_t id_;
  const pid_t pid_;
};

struct TabletReadTime {
  TabletId tablet_id;
  ReadHybridTime value;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(tablet_id, value);
  }
};

inline TransactionErrorCode GetTransactionErrorCode(const Status& status) {
  return status.ok() ? TransactionErrorCode::kNone : TransactionError(status).value();
}

// Momentos of a session's read points keyed by read_time_serial_no, so a paged read can restore
// the snapshot its scan started at.
constexpr uint64_t kInvalidReadTimeSerialNo = 0;

class ReadPointHistory {
 public:
  explicit ReadPointHistory(const PrefixLogger& prefix_logger) : prefix_logger_(prefix_logger) {}

  [[nodiscard]] bool Restore(ConsistentReadPoint* read_point, uint64_t read_time_serial_no) {
    if (read_time_serial_no == kInvalidReadTimeSerialNo) {
      return false;
    }
    auto result = false;
    if (const auto i = read_points_.find(read_time_serial_no);
        i != read_points_.end() && read_time_serial_no >= min_) {
      read_point->SetMomento(i->second);
      result = true;
    }
    VLOG_WITH_PREFIX(4) << "ReadPointHistory::Restore read_time_serial_no=" << read_time_serial_no
                        << " return " << result
                        << " read time is " << read_point->GetReadTime();
    return result;
  }

  void Save(const ConsistentReadPoint& read_point, uint64_t read_time_serial_no) {
    if (read_time_serial_no == kInvalidReadTimeSerialNo) {
      return;
    }
    auto momento = read_point.GetMomento();
    const auto& read_time = momento.read_time();
    DCHECK(read_time);
    VLOG_WITH_PREFIX(4) << "ReadPointHistory::Save read_time_serial_no=" << read_time_serial_no
                        << " read time is " << AsString(read_time);
    if (read_points_.empty()) {
      max_ = read_time_serial_no;
      min_ = read_time_serial_no;
    } else {
      min_ = std::min(min_, read_time_serial_no);
      max_ = std::max(max_, read_time_serial_no);
    }
    auto ipair = read_points_.try_emplace(read_time_serial_no, std::move(momento));
    if (!ipair.second) {
      // Potentially read time could be set to same read_time_serial_no multiple times.
      // It is expected that read time is the same or fresher (due to possible restart)
      // but not older.
      DCHECK(read_time.read >= ipair.first->second.read_time().read)
          << "Overwriting read_time_serial_no=" << read_time_serial_no
          << " with an older read time, given: " << AsString(read_time)
          << ", existing: " << AsString(ipair.first->second.read_time());
      ipair.first->second = std::move(momento);
    }
  }

  void Cleanup(uint64_t min) {
    VLOG_WITH_PREFIX(4) << "ReadTimeHistory::Cleanup " << min;
    if (read_points_.empty()) {
      return;
    }
    if (max_ < min) {
      VLOG_WITH_PREFIX(4) << "Clearing history [" << min_ << ", " << max_ << "]";
      read_points_.clear();
      return;
    }
    min_ = std::max(min_, min);
  }

 private:
  const PrefixLogger& LogPrefix() const { return prefix_logger_; }

  const PrefixLogger prefix_logger_;
  uint64_t min_ = 0;
  uint64_t max_ = 0;
  std::unordered_map<uint64_t, ConsistentReadPoint::Momento> read_points_;
};

Status CombineErrorsToStatus(const client::CollectedErrors& errors, const Status& status);

Status HandleOperationResponse(uint64_t session_id,
                               const client::YBPgsqlOp& op,
                               PgPerformResponseMsg* resp,
                               TabletReadTime* used_read_time);

template <class TableProvider>
Status GetTable(TableIdView table_id, TableProvider& provider, client::YBTablePtr* table) {
  if (*table && (**table).id() == table_id) {
    return Status::OK();
  }
  *table = VERIFY_RESULT(provider.Get(table_id));
  return Status::OK();
}

}  // namespace yb::tserver
