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

#include "yb/tserver/thin_client_service.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

#include <boost/container/small_vector.hpp>

#include "yb/ash/wait_state.h"

#include "yb/client/client.h"
#include "yb/client/error.h"
#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/yb_op.h"

#include "yb/common/pgsql_error.h"
#include "yb/common/wire_protocol.h"

#include "yb/gutil/casts.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_context.h"
#include "yb/rpc/scheduler.h"
#include "yb/rpc/thread_pool.h"

#include "yb/tserver/pg_client_session_util.h"
#include "yb/tserver/session_registry.h"
#include "yb/tserver/pg_mutation_counter.h"
#include "yb/tserver/pg_table_cache.h"

#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/std_util.h"
#include "yb/util/thread.h"

using namespace std::literals;

DECLARE_uint64(rpc_max_message_size);

namespace yb::tserver {

namespace {

// Thin clients only need the schema info of an opened table.
using ThinOpenTableQuery = OpenTableQueryBase<ThinOpenTableRequestPB, ThinOpenTableResponsePB>;

class ThinSession;
using ThinSessionPtr = std::shared_ptr<ThinSession>;

// One Perform call: holds the rpc context with its request/response, the table lookup result and
// the prepared operations.
class ThinPerformQuery : public std::enable_shared_from_this<ThinPerformQuery>,
                         public rpc::ThreadPoolTask,
                         public PgTablesQueryListener {
 public:
  using ContextHolder = rpc::TypedPBRpcContextHolder<
      LWThinPerformRequestPB, LWThinPerformResponsePB, rpc::RpcCallLWParamsImpl>;

  ThinPerformQuery(ThinSessionPtr&& session, ContextHolder&& context, rpc::Messenger& messenger)
      : session_(std::move(session)), context_(std::move(context)), messenger_(messenger),
        tid_(Thread::UniqueThreadId()), wait_state_ptr_(ash::WaitStateInfo::CurrentWaitState()) {}

  LWThinPerformRequestPB& req() { return context_.req(); }
  LWThinPerformResponsePB& resp() { return context_.resp(); }
  rpc::RpcContext& context() { return context_.context(); }
  rpc::Sidecars& sidecars() { return context().sidecars(); }

  ThreadSafeArenaPtr arena() {
    return SharedField(context().shared_params(), &resp().arena());
  }

  const PgTablesQueryResult& tables() const { return *tables_; }

  void Ready(const PgTablesQueryResult& tables) override {
    tables_ = tables;
    if (Thread::UniqueThreadId() == tid_) {
      Run();
    } else {
      retained_self_ = shared_from_this();
      messenger_.ThreadPool().Enqueue(this);
    }
  }

  void RespondFailure(const Status& status) {
    DCHECK(!status.ok());
    StatusToPB(status, resp().mutable_status());
    sidecars().Reset();
    context().RespondSuccess();
  }

  void RespondSuccess() { context().RespondSuccess(); }

  std::vector<std::shared_ptr<client::YBPgsqlOp>> ops;

 private:
  void Run() override;

  void Done(const Status& status) override {
    retained_self_ = nullptr;
  }

  const ThinSessionPtr session_;
  ContextHolder context_;
  rpc::Messenger& messenger_;
  const int64_t tid_;
  std::optional<PgTablesQueryResult> tables_;
  std::shared_ptr<ThinPerformQuery> retained_self_;

  // kept here in case the task is scheduled in another thread.
  const ash::WaitStateInfoPtr wait_state_ptr_;
};

using ThinPerformQueryPtr = std::shared_ptr<ThinPerformQuery>;

// A thin client session. It holds no request state of its own: each Perform builds its own
// YBSession, and the client owns the read point of a paged scan (it echoes the time its first page
// was served at). Batches are therefore independent: they are applied as they arrive, thin clients
// do not rely on ordering between in-flight Performs, and two clients that end up on the same
// session id cannot interfere with each other.
class ThinSession : public ClientSessionBase,
                    public std::enable_shared_from_this<ThinSession> {
 public:
  ThinSession(
      uint64_t id, client::YBClient* client, const scoped_refptr<ClockBase>& clock,
      PgTableCache& table_cache, PgMutationCounter* mutation_counter)
      : ClientSessionBase(id), client_(*client), clock_(clock), table_cache_(table_cache),
        mutation_counter_(mutation_counter), prefix_logger_(id) {}

  bool ReadyToShutdown() const override { return true; }

  void CompleteShutdown() override {}

  void Perform(const ThinPerformQueryPtr& query) {
    {
      std::lock_guard lock(mutex_);
      if (shutting_down_) {
        query->RespondFailure(STATUS(ShutdownInProgress, "Session is shutting down"));
        return;
      }
    }
    // Deliberately outside the lock: a batch owns its YBSession, so concurrent Performs on this
    // session share nothing. A batch admitted just as shutdown starts simply completes, which is
    // what StartShutdown already allows for in-flight flushes.
    auto status = DoPerform(query);
    if (!status.ok()) {
      query->RespondFailure(status);
    }
  }

  // In-flight flushes complete on their own.
  void StartShutdown(bool /* service_shutting_down */) override {
    VLOG_WITH_PREFIX(1) << "Starting shutdown";
    std::lock_guard lock(mutex_);
    shutting_down_ = true;
  }

 private:
  const PrefixLogger& LogPrefix() const { return prefix_logger_; }

  Status DoPerform(const ThinPerformQueryPtr& query) {
    auto& req = query->req();
    bool has_read = false;
    bool has_write = false;
    for (const auto& op : req.ops()) {
      (op.has_read() ? has_read : has_write) = true;
    }
    SCHECK(!(has_read && has_write), InvalidArgument, "Mixed read/write Perform batch");
    VLOG_WITH_PREFIX(2) << "Perform: " << req.ops().size() << (has_read ? " read" : " write")
                        << " op(s)";

    // One YBSession per batch. Sharing a single session across batches would mean sharing its read
    // point, deadline and -- worst -- its arena, which belongs to the response of whichever request
    // installed it, so an interleaved batch could allocate into another request's arena or outlive
    // it. Building one here keeps this session object stateless, so two clients that end up on the
    // same session id cannot interfere.
    auto session = std::make_shared<client::YBSession>(
        &client_, query->context().GetClientDeadline(), clock_, query->arena());
    session->SetForceConsistentRead(client::ForceConsistentRead::kTrue);
    session->set_allow_local_calls_in_curr_thread(false);
    CancelableScopeExit abort_se{[&session] { session->Abort(); }};

    const auto& read_time_options = req.read_time_options();
    if (has_read && read_time_options.has_read_time() && read_time_options.read_time().read_ht()) {
      // The client pins the snapshot: a paged scan echoes the time its first page was served at, so
      // every page reads at that one snapshot without the server tracking anything.
      session->SetReadPoint(ReadHybridTime::FromPB(read_time_options.read_time()));
    } else {
      // No read time: leave it unset so the tablet serving the op reads at its own safe time,
      // instead of this node stamping its current time and leaving the read to wait for safe time
      // to catch up to it. The chosen time comes back in the response, and the client echoes it on
      // continuations (see above), which is what keeps a paged scan on one snapshot.
      //
      // A batch that fans out over several tablets still reads at a single time: the batcher stamps
      // one before fanning out, since the session forces consistent reads (client/batcher.cc, the
      // force_consistent_read_ && groups.size() > 1 case). Writes take this path too, letting the
      // storage layer pick their read time for conflict resolution.
      session->SetReadPoint(ReadHybridTime());
    }

    const auto& tables = query->tables();
    client::YBTablePtr table;
    for (auto& op : *req.mutable_ops()) {
      if (op.has_read()) {
        auto& read = *op.mutable_read();
        RETURN_NOT_OK(GetTable(read.table_id(), tables, &table));
        query->ops.push_back(std::make_shared<client::YBPgsqlReadOp>(
            table, query->arena(), query->sidecars(), &read));
      } else {
        auto& write = *op.mutable_write();
        RETURN_NOT_OK(GetTable(write.table_id(), tables, &table));
        query->ops.push_back(std::make_shared<client::YBPgsqlWriteOp>(
            table, query->arena(), query->sidecars(), &write));
      }
      session->Apply(query->ops.back());
    }
    abort_se.Cancel();
    auto* session_ptr = session.get();
    session_ptr->FlushAsync(
        [shared_this = shared_from_this(), query, session = std::move(session)](
            client::FlushStatus* flush_status) {
          shared_this->FlushDone(query, flush_status);
        });
    return Status::OK();
  }

  void FlushDone(const ThinPerformQueryPtr& query, client::FlushStatus* flush_status) {
    auto status = CombineErrorsToStatus(flush_status->errors, flush_status->status);
    if (status.ok()) {
      status = ProcessResponse(query);
    }
    VLOG_WITH_PREFIX_AND_FUNC(2) << "status: " << status;
    if (status.ok()) {
      const size_t max_size = FLAGS_rpc_max_message_size;
      if (query->sidecars().size() > max_size) {
        status = STATUS_FORMAT(InvalidArgument,
                               "Sending too long RPC message ($0 bytes of data), limit: $1 bytes",
                               query->sidecars().size(), max_size);
      }
    }
    if (!status.ok()) {
      LOG_WITH_PREFIX(WARNING) << "Perform failed: " << status;
      query->RespondFailure(status);
      return;
    }
    query->RespondSuccess();
  }

  Status ProcessResponse(const ThinPerformQueryPtr& query) {
    int idx = -1;
    for (const auto& op : query->ops) {
      ++idx;
      const auto status = HandleOperationResponse(
          id(), *op, /* resp= */ nullptr, /* used_read_time= */ nullptr);
      if (!status.ok()) {
        if (PgsqlRequestStatus(status) == PgsqlResponsePB::PGSQL_STATUS_SCHEMA_VERSION_MISMATCH) {
          table_cache_.Invalidate(op->table()->id());
        }
        return status.CloneAndAddErrorCode(OpIndex(idx));
      }
      // Count writes: YSQL still queries these tables for low-frequency async work such as row GC,
      // so auto-analyze needs the mutation counts to keep their stats fresh.
      if (!op->read_only() && !op->table()->IsIndex() && mutation_counter_) {
        mutation_counter_->Increase(op->table()->id(), 1);
      }
    }
    auto& responses = *query->resp().mutable_responses();
    for (const auto& op : query->ops) {
      auto& op_resp = responses.push_back_ref(&op->response());
      if (const auto sidecar_index = op->sidecar_index(); sidecar_index) {
        op_resp.set_rows_data_sidecar(narrow_cast<int>(*sidecar_index));
      }
      // The paging state keeps its read time: that is how the client learns which snapshot its scan
      // was served at, to echo back on continuations.
      op_resp.set_partition_list_version(op->table()->GetPartitionListVersion());
    }
    return Status::OK();
  }

  client::YBClient& client_;
  const scoped_refptr<ClockBase> clock_;
  PgTableCache& table_cache_;
  PgMutationCounter* mutation_counter_;
  const PrefixLogger prefix_logger_;

  std::mutex mutex_;
  bool shutting_down_ GUARDED_BY(mutex_) = false;
};

void ThinPerformQuery::Run() {
  ADOPT_WAIT_STATE(wait_state_ptr_);
  SCOPED_WAIT_STATUS(OnCpu_Active);
  session_->Perform(shared_from_this());
}

}  // namespace

class ThinClientServiceImpl::Impl : public SessionRegistryContext {
 public:
  Impl(
      const std::shared_future<client::YBClient*>& client_future,
      const scoped_refptr<ClockBase>& clock, rpc::Messenger* messenger,
      PgMutationCounter* mutation_counter)
      : client_future_(client_future), clock_(clock), messenger_(*messenger),
        mutation_counter_(mutation_counter), table_cache_(client_future),
        session_registry_(&messenger->scheduler(), this) {}

  virtual ~Impl() {
    Shutdown();
  }

  Status Heartbeat(const ThinHeartbeatRequestPB& req, ThinHeartbeatResponsePB* resp) {
    if (req.session_id()) {
      return ResultToStatus(session_registry_.Get(req.session_id()));
    }

    auto session_id = session_registry_.NewSessionId();
    auto session = std::make_shared<ThinSession>(
        session_id, &client(), clock_, table_cache_, mutation_counter_);
    LOG(INFO) << "Opening thin client session " << session_id;
    resp->set_session_id(session_id);
    return session_registry_.Insert(std::move(session));
  }

  void OpenTable(
      const ThinOpenTableRequestPB& req, ThinOpenTableResponsePB* resp, rpc::RpcContext context) {
    auto query = std::make_shared<ThinOpenTableQuery>(
        MakeTypedPBRpcContextHolder(req, resp, std::move(context)));
    // Always refetch: this cache is not wired into DDL invalidation, and thin clients open a
    // table once per handle.
    table_cache_.GetTables(std::span(&req.table_id(), 1), query, {.reopen = true});
  }

  void Perform(
      LWThinPerformRequestPB* req, LWThinPerformResponsePB* resp, rpc::RpcContext* context) {
    auto session = session_registry_.Get(req->session_id());
    if (!session.ok()) {
      Respond(session.status(), resp, context);
      return;
    }
    boost::container::small_vector<TableId, 4> table_ids;
    for (const auto& op : req->ops()) {
      AddIfMissing(table_ids, op.has_read() ? op.read().table_id() : op.write().table_id());
    }
    auto query = std::make_shared<ThinPerformQuery>(
        std::move(*session), MakeTypedPBRpcContextHolder(*req, resp, std::move(*context)),
        messenger_);
    table_cache_.GetTables(table_ids, query);
  }

  void Shutdown() {
    LOG(INFO) << "Shutting down ThinClientService with " << session_registry_.Count()
              << " session(s)";
    session_registry_.Shutdown();
  }

 private:
  client::YBClient& client() { return *client_future_.get(); }

  std::shared_future<client::YBClient*> client_future_;
  const scoped_refptr<ClockBase> clock_;
  rpc::Messenger& messenger_;
  PgMutationCounter* mutation_counter_;
  PgTableCache table_cache_;
  SessionRegistry<ThinSession> session_registry_;
};

ThinClientServiceImpl::ThinClientServiceImpl(
    const std::shared_future<client::YBClient*>& client_future,
    const scoped_refptr<ClockBase>& clock,
    const scoped_refptr<MetricEntity>& entity, rpc::Messenger* messenger,
    PgMutationCounter* pg_node_level_mutation_counter)
    : ThinClientServiceIf(entity),
      impl_(new Impl(client_future, clock, messenger, pg_node_level_mutation_counter)) {}

ThinClientServiceImpl::~ThinClientServiceImpl() = default;

void ThinClientServiceImpl::Heartbeat(
    const ThinHeartbeatRequestPB* req, ThinHeartbeatResponsePB* resp, rpc::RpcContext context) {
  Respond(impl_->Heartbeat(*req, resp), resp, &context);
}

void ThinClientServiceImpl::OpenTable(
    const ThinOpenTableRequestPB* req, ThinOpenTableResponsePB* resp, rpc::RpcContext context) {
  impl_->OpenTable(*req, resp, std::move(context));
}

void ThinClientServiceImpl::Perform(
    const LWThinPerformRequestPB* req, LWThinPerformResponsePB* resp, rpc::RpcContext context) {
  impl_->Perform(const_cast<LWThinPerformRequestPB*>(req), resp, &context);
}

void ThinClientServiceImpl::Shutdown() {
  impl_->Shutdown();
}

}  // namespace yb::tserver
