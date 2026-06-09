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
#include <functional>

#include <boost/container/small_vector.hpp>

#include "yb/common/pg_system_attr.h"
#include "yb/common/transaction_error.h"

#include "yb/util/std_util.h"

#include "yb/yql/pggate/pg_doc_op.h"
#include "yb/yql/pggate/pg_doc_op_fetch_stream.h"
#include "yb/yql/pggate/pg_session.h"
#include "yb/yql/pggate/pg_table.h"

namespace yb::pggate {
namespace {

Status ExecuteInit(PgDocOp& op, const YbctidReader::Options& options) {
  auto exec_params = op.ExecParameters();
  exec_params.rowmark = options.rowmark.value_or(exec_params.rowmark);
  exec_params.pg_wait_policy = options.pg_wait_policy.value_or(exec_params.pg_wait_policy);
  exec_params.docdb_wait_policy = options.docdb_wait_policy.value_or(exec_params.docdb_wait_policy);
  return op.ExecuteInit(&exec_params);
}

struct PgsqlReadOpWithPgTable {
  PgsqlReadOpWithPgTable(
      ThreadSafeArena* arena, const PgTableDescPtr& descr,
      const YbcPgTableLocalityInfo& locality_info, PgsqlMetricsCaptureType metrics_capture)
      : table(descr), read_op(arena, *table, locality_info, metrics_capture) {}

  PgTable table;
  PgsqlReadOp read_op;
};

struct Ybctids {
  boost::container::small_vector<LightweightTableYbctid, 8> values;
  DocResultYbctidRetention retention;

  void Clear() {
    values.clear();
    retention.Clear();
  }
};

Result<bool> ProcessYbctids(Ybctids& ybctids, PgDocReadOp& op, bool skip_locked) {
  auto res = op.ResultStream().ProcessNextYbctids(
      [&ybctids, &op](Slice ybctid) {
        ybctids.values.emplace_back(op.table()->relfilenode_id().object_oid, ybctid);
      },
      &ybctids.retention);
  if (!res.ok() &&
      skip_locked &&
      TransactionError::ValueFromStatus(res.status()) == TransactionErrorCode::kSkipLocking) {
    VLOG(5) << "skip locked detected";
    return false;
  }
  return res;
}

class RequestCollector {
 public:
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

  [[nodiscard]] auto operator()(std::span<const PgsqlOpPtr> ops, const PgTableDesc& table) {
    for (const auto& o : ops) {
      ops_.emplace_back(o, &table);
    }
    return PgDocResponse{std::make_unique<ResponseProvider>(provider_state_)};
  }

  Status Flush(PgSession& session, std::optional<PgSessionRunOperationMarker> marker) {
    auto i = ops_.begin();
    PgDocResponse response(
        VERIFY_RESULT(session.RunAsync(make_lw_function(
            [&i, end = ops_.end()] {
              using TO = PgSession::TableOperation<PgsqlOpPtr>;
              if (i == end) {
                return TO{};
              }
              auto& info = *i++;
              return TO{.operation = &info.operation, .table = info.table};
            }), {.marker = marker})),
        {TableType::USER, IsForWritePgDoc::kFalse, IsOpBuffered::kFalse});
    *provider_state_ = VERIFY_RESULT(response.Get(session));
    return Status::OK();
  }

 private:
  struct OperationInfo {
    PgsqlOpPtr operation;
    const PgTableDesc* table;
  };

  ResponseProvider::StatePtr provider_state_{std::make_shared<ResponseProvider::State>()};
  boost::container::small_vector<OperationInfo, 16> ops_;
};

class Sender {
 public:
  Sender(std::optional<PgSessionRunOperationMarker> marker, bool skip_collecting)
      : marker_(marker) {
    if (!skip_collecting) {
      collector_.emplace();
    }
  }

  Result<PgDocResponse> operator()(
      PgSession* session, std::span<const PgsqlOpPtr> ops, const PgTableDesc& table,
      const PgSession::RunOptions& options, IsForWritePgDoc is_write) {
    DCHECK(!is_write);
    if (collector_) {
      // For now Collector can work only with a new in txn limit set to the current time
      // for each batch of ops. It doesn't use a single in txn limit for all read ops
      // in a statement.
      // TODO: Explain why is this the case because it differs from requirement 1 in
      // src/yb/yql/pggate/README
      DCHECK(!PgSession::RunOptions{}.in_txn_limit);
      RSTATUS_DCHECK(
          options == PgSession::RunOptions{},
          IllegalState, "Only default run options is expected, got $0", options);
      return (*collector_)(ops, table);
    }
    DCHECK(!options.marker);
    auto actual_options = options;
    actual_options.marker = marker_;
    return PgDocOp::DefaultSender(session, ops, table, actual_options, IsForWritePgDoc::kFalse);
  }

  Status Flush(PgSession& session) {
    if (collector_) {
      RETURN_NOT_OK(collector_->Flush(session, marker_));
    }
    collector_.reset();
    return Status::OK();
  }

 private:
  std::optional<RequestCollector> collector_;
  const std::optional<PgSessionRunOperationMarker> marker_;
};

} // namespace

class YbctidReader::Impl {
 public:
  explicit Impl(PgSession& session) : session_{session} {}

  Result<std::span<LightweightTableYbctid>> Read(
      PgOid database_id, const TableLocalityMap& tables_locality, const Options& options) {
    constexpr auto kSkipLockedPolicy = std::optional(WAIT_SKIP);
    auto& ybctids = ybctids_.values;
    const auto is_skip_locked = (options.docdb_wait_policy == kSkipLockedPolicy);
    if (!is_skip_locked) {
      // Group the items by the table ID.
      std::ranges::sort(
          ybctids, [](const auto& a, const auto& b) { return a.table_id < b.table_id; });
    }

    auto arena = std::make_shared<ThreadSafeArena>();
    Sender sender(options.run_marker, /* skip_collecting = */ is_skip_locked);
    auto request_sender = std::bind_front(&Sender::operator(), &sender);

    // Start all the doc_ops to read from docdb in parallel, one doc_op per table ID.
    // Each doc_op will use request_sender to send all the requests with single perform RPC.
    auto shared_session = session_.shared_from_this();
    for (auto it = ybctids.begin(), end = ybctids.end(); it != end;) {
      const auto table_id = it->table_id;
      auto desc = VERIFY_RESULT(session_.LoadTable(PgObjectId(database_id, table_id)));
      auto metrics_capture = session_.metrics().metrics_capture();
      auto read_op_with_table = std::make_shared<PgsqlReadOpWithPgTable>(
          arena.get(), desc, tables_locality.Get(table_id), metrics_capture);
      auto read_op = SharedField(read_op_with_table, &read_op_with_table->read_op);
      auto* expr_pb = read_op->read_request().add_targets();
      expr_pb->set_column_id(std::to_underlying(PgSystemAttrNum::kYBTupleId));
      doc_ops_.push_back(std::make_unique<PgDocReadOp>(
          shared_session, &read_op_with_table->table, std::move(read_op), request_sender));
      auto& doc_op = *doc_ops_.back();
      RETURN_NOT_OK(ExecuteInit(doc_op, options));
      const auto ybctid_start = it++;
      if (!is_skip_locked) {
        for (const auto table_id = ybctid_start->table_id; it != end && it->table_id == table_id;) {
          ++it;
        }
      }
      const auto ybctid_end = it;
      RSTATUS_DCHECK(
          VERIFY_RESULT(doc_op.PopulateByYbctidOps({
              make_lw_function([i = ybctid_start, &ybctid_end] mutable {
                return i != ybctid_end ? Slice((i++)->ybctid) : Slice();
              }),
              static_cast<size_t>(ybctid_end - ybctid_start)})),
          IllegalState, "Failed to create requests, can't fetch");
      RETURN_NOT_OK(doc_op.Execute());
    }

    RETURN_NOT_OK(sender.Flush(session_));
    // Collect the results from the docdb ops.
    ybctids.clear();
    for (auto& op : doc_ops_) {
      for (;;) {
        if (!VERIFY_RESULT(ProcessYbctids(ybctids_, *op, is_skip_locked))) {
          break;
        }
      }
    }

    return std::span(ybctids);
  }

  [[nodiscard]] size_t StartNewBatch(size_t capacity) {
    const auto signature = ++active_batch_accessor_signature_;
    Clear();
    if (capacity) {
      ybctids_.values.reserve(capacity);
    }
    return signature;
  }

  void Add(const LightweightTableYbctid& ybctid) { ybctids_.values.push_back(ybctid); }

  [[nodiscard]] size_t active_batch_accessor_signature() const {
      return active_batch_accessor_signature_;
  }

  void Clear() {
    doc_ops_.clear();
    ybctids_.Clear();
  }

 private:
  PgSession& session_;
  boost::container::small_vector<std::unique_ptr<PgDocReadOp>, 8> doc_ops_;
  Ybctids ybctids_;
  size_t active_batch_accessor_signature_{0};

  DISALLOW_COPY_AND_ASSIGN(Impl);
};

YbctidReader::BatchAccessor::~BatchAccessor() {
  if (PREDICT_TRUE(IsActive())) {
    impl_.Clear();
  }
}

void YbctidReader::BatchAccessor::Add(const LightweightTableYbctid& ybctid) {
  if (PREDICT_TRUE(IsActive())) {
    impl_.Add(ybctid);
  } else {
    DCHECK(false) << "The batch is inactive";
  }
}

bool YbctidReader::BatchAccessor::IsActive() const {
  return signature_ == impl_.active_batch_accessor_signature();
}

YbctidReader::ReadResult YbctidReader::BatchAccessor::Read(
    PgOid database_id, const TableLocalityMap& tables_locality, const Options& options) {
  RSTATUS_DCHECK(IsActive(), IllegalState, "Read from inactive batch is not allowed");
  return impl_.Read(database_id, tables_locality, options);
}

YbctidReader::YbctidReader(PgSession& session) : impl_(new Impl(session)) {}

YbctidReader::~YbctidReader() = default;

YbctidReader::BatchAccessor YbctidReader::StartNewBatch(size_t capacity) {
  return {*impl_, impl_->StartNewBatch(capacity)};
}

} // namespace yb::pggate
