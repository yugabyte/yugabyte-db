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

#include "yb/yql/pggate/pg_ybctid_reader.h"

#include <algorithm>

#include "yb/common/pg_system_attr.h"

#include "yb/util/std_util.h"

#include "yb/yql/pggate/pg_table.h"
#include "yb/yql/pggate/pg_session.h"

namespace yb::pggate {
namespace {

struct PgsqlReadOpWithPgTable {
  PgsqlReadOpWithPgTable(
      ThreadSafeArena* arena, const PgTableDescPtr& descr,
      const YbcPgTableLocalityInfo& locality_info, PgsqlMetricsCaptureType metrics_capture)
      : table(descr), read_op(arena, *table, locality_info, metrics_capture) {
  }
  PgTable table;
  PgsqlReadOp read_op;
};

// Helper class to collect operations from multiple doc_ops and send them with a single perform RPC.
class PrecastRequestSender {
  // Struct stores operation and table for futher sending this operation
  // with the 'PgSession::RunAsync' method.
  struct OperationInfo {
    OperationInfo(const PgsqlOpPtr& operation_, const PgTableDesc& table_)
        : operation(operation_), table(&table_) {}
    PgsqlOpPtr operation;
    const PgTableDesc* table;
  };

  class ResponseProvider : public PgDocResponse::Provider {
   public:
    // Shared state among different instances of the 'PgDocResponse' object returned by the 'Send'
    // method. Response field will be initialized when all collected operations will be sent by the
    // call of 'TransmitCollected' method.
    using State = PgDocResponse::Data;
    using StatePtr = std::shared_ptr<State>;

    explicit ResponseProvider(const StatePtr& state)
        : state_(state) {}

    Result<PgDocResponse::Data> Get() override {
      SCHECK(state_->response, IllegalState, "Response is not set");
      return *state_;
    }

   private:
    StatePtr state_;
  };

 public:
  Result<PgDocResponse> Send(
      PgSession& session, const PgsqlOpPtr* ops, size_t ops_count, const PgTableDesc& table,
      HybridTime in_txn_limit) {
    if (!collecting_mode_) {
      return PgDocOp::DefaultSender(
          &session, ops, ops_count, table, in_txn_limit,
          ForceNonBufferable::kFalse, IsForWritePgDoc::kFalse);
    }
    // For now PrecastRequestSender can work only with a new in txn limit set to the current time
    // for each batch of ops. It doesn't use a single in txn limit for all read ops in a statement.
    // TODO: Explain why is this the case because it differs from requirement 1 in
    // src/yb/yql/pggate/README
    RSTATUS_DCHECK(!in_txn_limit, IllegalState, "Only zero is expected");
    for (auto end = ops + ops_count; ops != end; ++ops) {
      ops_.emplace_back(*ops, table);
    }
    if (!provider_state_) {
      provider_state_ = std::make_shared<ResponseProvider::State>();
    }
    return PgDocResponse(std::make_unique<ResponseProvider>(provider_state_));
  }

  Status TransmitCollected(PgSession& session) {
    auto res = DoTransmitCollected(session);
    ops_.clear();
    provider_state_.reset();
    return res;
  }

  void DisableCollecting() {
    DCHECK(ops_.empty());
    collecting_mode_ = false;
  }

 private:
  Status DoTransmitCollected(PgSession& session) {
    auto i = ops_.begin();
    PgDocResponse response(VERIFY_RESULT(session.RunAsync(make_lw_function(
        [&i, end = ops_.end()] {
          using TO = PgSession::TableOperation<PgsqlOpPtr>;
          if (i == end) {
            return TO();
          }
          auto& info = *i++;
          return TO{.operation = &info.operation, .table = info.table};
        }), HybridTime())),
        {TableType::USER, IsForWritePgDoc::kFalse, IsOpBuffered::kFalse});
    *provider_state_ = VERIFY_RESULT(response.Get(session));
    return Status::OK();
  }

  bool collecting_mode_ = true;
  ResponseProvider::StatePtr provider_state_;
  boost::container::small_vector<OperationInfo, 16> ops_;
};

} // namespace

YbctidReader::YbctidReader(const PgSessionPtr& session)
    : session_(session) {}

YbctidReader::~YbctidReader() = default;

Result<std::span<LightweightTableYbctid>> YbctidReader::Read(
    PgOid database_id, const TableLocalityMap& tables_locality,
    const ExecParametersMutator& exec_params_mutator) {
  // Group the items by the table ID.
  std::ranges::sort(ybctids_, [](const auto& a, const auto& b) { return a.table_id < b.table_id; });

  auto arena = std::make_shared<ThreadSafeArena>();

  PrecastRequestSender precast_sender;
  boost::container::small_vector<std::unique_ptr<PgDocReadOp>, 16> doc_ops;
  auto request_sender = [&precast_sender](
      PgSession* session, const PgsqlOpPtr* ops, size_t ops_count, const PgTableDesc& table,
      HybridTime in_txn_limit, ForceNonBufferable force_non_bufferable, IsForWritePgDoc is_write) {
    DCHECK(!force_non_bufferable);
    DCHECK(!is_write);
    return precast_sender.Send(*session, ops, ops_count, table, in_txn_limit);
  };
  // Start all the doc_ops to read from docdb in parallel, one doc_op per table ID.
  // Each doc_op will use request_sender to send all the requests with single perform RPC.
  for (auto it = ybctids_.begin(), end = ybctids_.end(); it != end;) {
    const auto table_id = it->table_id;
    auto desc = VERIFY_RESULT(session_->LoadTable(PgObjectId(database_id, table_id)));
    auto metrics_capture = session_->metrics().metrics_capture();
    auto read_op_with_table = std::make_shared<PgsqlReadOpWithPgTable>(
        arena.get(), desc, tables_locality.Get(table_id), metrics_capture);
    auto read_op = SharedField(read_op_with_table, &read_op_with_table->read_op);
    auto* expr_pb = read_op->read_request().add_targets();
    expr_pb->set_column_id(std::to_underlying(PgSystemAttrNum::kYBTupleId));
    doc_ops.push_back(std::make_unique<PgDocReadOp>(
        session_, &read_op_with_table->table, std::move(read_op), request_sender));
    auto& doc_op = *doc_ops.back();
    auto exec_params = doc_op.ExecParameters();
    exec_params_mutator(exec_params);
    RETURN_NOT_OK(doc_op.ExecuteInit(&exec_params));
    // Populate doc_op with ybctids which belong to current table.
    RSTATUS_DCHECK(VERIFY_RESULT(doc_op.PopulateByYbctidOps({make_lw_function([&it, table_id, end] {
      return it != end && it->table_id == table_id ? Slice((it++)->ybctid) : Slice();
    }), static_cast<size_t>(end - it)})), IllegalState, "Failed to create requests, can't fetch");
    RETURN_NOT_OK(doc_op.Execute());
  }

  RETURN_NOT_OK(precast_sender.TransmitCollected(*session_));
  // Disable further request collecting as in the vast majority of cases new requests will not be
  // initiated because requests for all ybctids has already been sent. But in case of dynamic
  // splitting new requests might be sent. They will be sent and processed as usual (i.e. request
  // of each doc_op will be sent individually).
  precast_sender.DisableCollecting();
  // Collect the results from the docdb ops.
  ybctids_.clear();
  for (auto& doc_op : doc_ops) {
    for(;;) {
      doc_op->ResultStream().HoldResults(holders_);
      if (!VERIFY_RESULT(doc_op->ResultStream().ProcessNextYbctids(
        [this, object_oid = doc_op->table()->relfilenode_id().object_oid](Slice ybctid) {
          ybctids_.emplace_back(object_oid, ybctid);
        }))) {
        break;
      }
    }
  }

  return std::span(ybctids_);
}

} // namespace yb::pggate
