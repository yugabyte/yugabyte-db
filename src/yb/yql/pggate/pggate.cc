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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pggate.h"

#include <algorithm>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/container/small_vector.hpp>

#include <ev++.h>

#include "yb/client/client_utils.h"
#include "yb/client/table_info.h"

#include "yb/dockv/partition.h"
#include "yb/common/pg_system_attr.h"
#include "yb/common/pgsql_protocol.pb.h"
#include "yb/common/schema.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/primitive_value.h"
#include "yb/dockv/value_type.h"

#include "yb/gutil/casts.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"
#include "yb/rpc/secure_stream.h"

#include "yb/rpc/secure.h"

#include "yb/tserver/pg_client.pb.h"
#include "yb/tserver/tserver_shared_mem.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/enums.h"
#include "yb/util/format.h"
#include "yb/util/range.h"
#include "yb/util/status_format.h"
#include "yb/util/thread.h"

#include "yb/yql/pggate/pg_column.h"
#include "yb/yql/pggate/pg_ddl.h"
#include "yb/yql/pggate/pg_delete.h"
#include "yb/yql/pggate/pg_dml.h"
#include "yb/yql/pggate/pg_dml_read.h"
#include "yb/yql/pggate/pg_dml_write.h"
#include "yb/yql/pggate/pg_function.h"
#include "yb/yql/pggate/pg_insert.h"
#include "yb/yql/pggate/pg_memctx.h"
#include "yb/yql/pggate/pg_sample.h"
#include "yb/yql/pggate/pg_select.h"
#include "yb/yql/pggate/pg_select_index.h"
#include "yb/yql/pggate/pg_session.h"
#include "yb/yql/pggate/pg_statement.h"
#include "yb/yql/pggate/pg_table.h"
#include "yb/yql/pggate/pg_tabledesc.h"
#include "yb/yql/pggate/pg_truncate_colocated.h"
#include "yb/yql/pggate/pg_txn_manager.h"
#include "yb/yql/pggate/pg_update.h"
#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pggate/ybc_pggate.h"

using namespace std::literals;
using std::string;
using std::vector;

DECLARE_bool(use_node_to_node_encryption);
DECLARE_string(certs_dir);
DECLARE_bool(node_to_node_encryption_use_client_certificates);
DECLARE_int32(backfill_index_client_rpc_timeout_ms);
DECLARE_uint32(wait_for_ysql_backends_catalog_version_client_master_rpc_margin_ms);
DECLARE_uint32(wait_for_ysql_backends_catalog_version_client_master_rpc_timeout_ms);

DEFINE_RUNTIME_PREVIEW_bool(ysql_pack_inserted_value, false,
     "Enabled packing inserted columns into a single packed value in postgres layer.");

namespace yb::pggate {
namespace {

struct TableHolder {
  explicit TableHolder(const PgTableDescPtr& descr) : table_(descr) {}
  PgTable table_;
};

class PgsqlReadOpWithPgTable : private TableHolder, public PgsqlReadOp {
 public:
  PgsqlReadOpWithPgTable(ThreadSafeArena* arena, const PgTableDescPtr& descr, bool is_region_local,
                         PgsqlMetricsCaptureType metrics_capture)
      : TableHolder(descr), PgsqlReadOp(arena, *table_, is_region_local, metrics_capture) {}

  PgTable& table() {
    return table_;
  }
};

Status AddColumn(PgCreateTable* pg_stmt, const char *attr_name, int attr_num,
                 const YBCPgTypeEntity *attr_type, bool is_hash, bool is_range,
                 bool is_desc, bool is_nulls_first) {
  using SortingType = SortingType;
  SortingType sorting_type = SortingType::kNotSpecified;

  if (!is_hash && is_range) {
    if (is_desc) {
      sorting_type = is_nulls_first ? SortingType::kDescending : SortingType::kDescendingNullsLast;
    } else {
      sorting_type = is_nulls_first ? SortingType::kAscending : SortingType::kAscendingNullsLast;
    }
  }

  return pg_stmt->AddColumn(attr_name, attr_num, attr_type, is_hash, is_range, sorting_type);
}

Result<PgApiContext::MessengerHolder> BuildMessenger(
    const string& client_name,
    int32_t num_reactors,
    const scoped_refptr<MetricEntity>& metric_entity,
    const std::shared_ptr<MemTracker>& parent_mem_tracker) {
  std::unique_ptr<rpc::SecureContext> secure_context;
  if (FLAGS_use_node_to_node_encryption) {
    secure_context = VERIFY_RESULT(rpc::CreateSecureContext(
        FLAGS_certs_dir,
        rpc::UseClientCerts(FLAGS_node_to_node_encryption_use_client_certificates)));
  }
  auto messenger = VERIFY_RESULT(client::CreateClientMessenger(
      client_name, num_reactors, metric_entity, parent_mem_tracker, secure_context.get()));
  return PgApiContext::MessengerHolder{std::move(secure_context), std::move(messenger)};
}

tserver::TServerSharedObject BuildTServerSharedObject() {
  VLOG(1) << __func__
          << ": " << YBCIsInitDbModeEnvVarSet()
          << ", " << FLAGS_pggate_tserver_shm_fd;
  LOG_IF(DFATAL, FLAGS_pggate_tserver_shm_fd == -1) << "pggate_tserver_shm_fd is not specified";
  return CHECK_RESULT(tserver::TServerSharedObject::OpenReadOnly(FLAGS_pggate_tserver_shm_fd));
}

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
        {TableType::USER, IsForWritePgDoc::kFalse});
    *provider_state_ = VERIFY_RESULT(response.Get(session));
    return Status::OK();
  }

  bool collecting_mode_ = true;
  ResponseProvider::StatePtr provider_state_;
  boost::container::small_vector<OperationInfo, 16> ops_;
};

using ExecParametersMutator = std::function<void(PgExecParameters*)>;

Status FetchExistingYbctids(const PgSession::ScopedRefPtr& session,
                            PgOid database_id,
                            TableYbctidVector* ybctids,
                            const OidSet& region_local_tables,
                            const ExecParametersMutator& exec_params_mutator) {
  // Group the items by the table ID.
  std::sort(ybctids->begin(), ybctids->end(), [](const auto& a, const auto& b) {
    return a.table_id < b.table_id;
  });

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
  for (auto it = ybctids->begin(), end = ybctids->end(); it != end;) {
    const auto table_id = it->table_id;
    auto desc = VERIFY_RESULT(session->LoadTable(PgObjectId(database_id, table_id)));
    bool is_region_local = region_local_tables.find(table_id) != region_local_tables.end();
    auto metrics_capture = session->metrics().metrics_capture();
    auto read_op = std::make_shared<PgsqlReadOpWithPgTable>(
        arena.get(), desc, is_region_local, metrics_capture);

    auto* expr_pb = read_op->read_request().add_targets();
    expr_pb->set_column_id(to_underlying(PgSystemAttrNum::kYBTupleId));
    doc_ops.push_back(std::make_unique<PgDocReadOp>(
        session, &read_op->table(), std::move(read_op), request_sender));
    auto& doc_op = *doc_ops.back();
    auto exec_params = doc_op.ExecParameters();
    exec_params_mutator(&exec_params);
    RETURN_NOT_OK(doc_op.ExecuteInit(&exec_params));
    // Populate doc_op with ybctids which belong to current table.
    RETURN_NOT_OK(doc_op.PopulateByYbctidOps({make_lw_function([&it, table_id, end] {
      return it != end && it->table_id == table_id ? Slice((it++)->ybctid) : Slice();
    }), static_cast<size_t>(end - it)}));
    RETURN_NOT_OK(doc_op.Execute());
  }

  RETURN_NOT_OK(precast_sender.TransmitCollected(*session));
  // Disable further request collecting as in the vast majority of cases new requests will not be
  // initiated because requests for all ybctids has already been sent. But in case of dynamic
  // splitting new requests might be sent. They will be sent and processed as usual (i.e. request
  // of each doc_op will be sent individually).
  precast_sender.DisableCollecting();
  // Collect the results from the docdb ops.
  ybctids->clear();
  for (auto& it : doc_ops) {
    for (;;) {
      auto rowsets = VERIFY_RESULT(it->GetResult());
      if (rowsets.empty()) {
        break;
      }
      for (auto& row : rowsets) {
        RETURN_NOT_OK(row.ProcessSystemColumns());
        for (const auto& ybctid : row.ybctids()) {
          ybctids->emplace_back(it->table()->relfilenode_id().object_oid, ybctid.ToBuffer());
        }
      }
    }
  }

  return Status::OK();
}

auto MakeYbctidReaderForExplicitRowLock(const PgSession::ScopedRefPtr& session) {
  return [&session](TableYbctidVector* ybctids,
                    const ExplicitRowLockBuffer::Info& info,
                    const OidSet& region_local_tables) {
    return FetchExistingYbctids(
        session, info.database_id, ybctids, region_local_tables,
        [&info](PgExecParameters* exec_params) {
          exec_params->rowmark = info.rowmark;
          exec_params->pg_wait_policy = info.pg_wait_policy;
          exec_params->docdb_wait_policy = info.docdb_wait_policy;
        });
  };
}

} // namespace

//--------------------------------------------------------------------------------------------------

size_t PgMemctxHasher::operator()(const std::unique_ptr<PgMemctx>& value) const {
  return (*this)(value.get());
}

size_t PgMemctxHasher::operator()(PgMemctx* value) const {
  return std::hash<PgMemctx*>()(value);
}

//--------------------------------------------------------------------------------------------------

PgApiContext::MessengerHolder::MessengerHolder(
    std::unique_ptr<rpc::SecureContext> security_context_,
    std::unique_ptr<rpc::Messenger> messenger_)
    : security_context(std::move(security_context_)), messenger(std::move(messenger_)) {
}

PgApiContext::MessengerHolder::MessengerHolder(MessengerHolder&& rhs)
    : security_context(std::move(rhs.security_context)),
      messenger(std::move(rhs.messenger)) {
}

PgApiContext::MessengerHolder::~MessengerHolder() {
}

PgApiContext::PgApiContext()
    : metric_registry(new MetricRegistry()),
      metric_entity(METRIC_ENTITY_server.Instantiate(metric_registry.get(), "yb.pggate")),
      mem_tracker(MemTracker::CreateTracker("PostgreSQL")),
      messenger_holder(CHECK_RESULT(BuildMessenger("pggate_ybclient",
                                                   FLAGS_pggate_ybclient_reactor_threads,
                                                   metric_entity,
                                                   mem_tracker))),
      proxy_cache(std::make_unique<rpc::ProxyCache>(messenger_holder.messenger.get())) {
}

PgApiContext::PgApiContext(PgApiContext&&) = default;

PgApiContext::~PgApiContext() = default;

//--------------------------------------------------------------------------------------------------

// Helper class to shutdown RPC messenger in async-signal-safe manner.
// On interrupt request class resumes separate thread is async-signal-safe manner to perform
// non-async-signal-safe messenger shutdown.
class PgApiImpl::Interrupter {
 public:
  explicit Interrupter(rpc::Messenger* messenger)
      : messenger_(*messenger) {
  }

  ~Interrupter() {
    if (thread_) {
      Interrupt();
      CHECK_OK(ThreadJoiner(thread_.get()).Join());
      thread_.reset();
    }
  }

  Status Start() {
    async_.set(loop_);
    async_.set<Interrupter, &Interrupter::AsyncHandler>(this);
    async_.start();
    return yb::Thread::Create(
        "pgapi interrupter", "pgapi interrupter", &Interrupter::RunThread, this, &thread_);
  }

  void Interrupt() {
    async_.send();
  }

 private:
  void AsyncHandler(ev::async& async, int events) { // NOLINT
    messenger_.Shutdown();
    loop_.break_loop();
  }

  void RunThread() {
    loop_.run();
  }

  rpc::Messenger& messenger_;
  ev::dynamic_loop loop_;
  ev::async async_;
  scoped_refptr<yb::Thread> thread_;
};

//--------------------------------------------------------------------------------------------------

void PgApiImpl::TupleIdBuilder::Prepare() {
  // Arena's start block size (kStartBlockSize) is 4kB, there is no reason to reset it after
  // building each key. Reset it after building 8 keys.
  if (((++counter_) & 0x7) == 0) {
    arena_.Reset(ResetMode::kKeepFirst);
  }
  doc_key_.Clear();
}

Result<dockv::KeyBytes> PgApiImpl::TupleIdBuilder::Build(
    PgSession* session, const YBCPgYBTupleIdDescriptor& descr) {
  Prepare();
  auto target_desc = VERIFY_RESULT(session->LoadTable(
      PgObjectId(descr.database_oid, descr.table_relfilenode_oid)));
  const auto num_keys = target_desc->num_key_columns();
  SCHECK_EQ(
      descr.nattrs, num_keys,
      Corruption, "Number of key components does not match column description");
  const auto num_hash_keys = target_desc->num_hash_key_columns();
  const auto num_range_keys = num_keys - num_hash_keys;
  LWPgsqlExpressionPB temp_expr_pb(&arena_);
  ArenaList<LWPgsqlExpressionPB> hashed_values(&arena_);
  auto& hashed_components = doc_key_.hashed_group();
  auto& range_components = doc_key_.range_group();
  hashed_components.reserve(num_hash_keys);
  range_components.reserve(num_range_keys);
  auto* attrs_end = descr.attrs + descr.nattrs;
  std::string new_row_id_holder;
  // DocDB API requires that partition columns must be listed in their created-order.
  // Order from target_desc should be used as attributes sequence may have different order.
  for (auto i : Range(num_keys)) {
    PgColumn column(target_desc->schema(), i);
    const auto* attr = std::find_if(
        descr.attrs, attrs_end,
        [attr_num = column.attr_num()](const auto& attr) {
            return attr_num == attr.attr_num;
        });
    SCHECK(
        attr != attrs_end,
        InvalidArgument, Format("Key attribute number $0 not found", column.attr_num()));

    // Suppose we are processing range component
    auto* values = &range_components;
    auto* expr_pb = &temp_expr_pb;
    if (column.is_partition()) {
      // Hashed component
      values = &hashed_components;
      expr_pb = &hashed_values.emplace_back();
    }

    if (attr->is_null) {
      values->emplace_back(dockv::KeyEntryType::kNullLow);
      continue;
    }
    if (attr->attr_num == to_underlying(PgSystemAttrNum::kYBRowId)) {
      SCHECK(new_row_id_holder.empty(), Corruption, "Multiple kYBRowId attribute detected");
      new_row_id_holder = session->GenerateNewYbrowid();
      expr_pb->mutable_value()->ref_binary_value(new_row_id_holder);
    } else {
      const auto& collation_info = attr->collation_info;
      auto value = arena_.NewObject<PgConstant>(
          &arena_, attr->type_entity, collation_info.collate_is_valid_non_c,
          collation_info.sortkey, attr->datum, false);
      SCHECK_EQ(
          column.internal_type(), value->internal_type(),
          Corruption, "Attribute value type does not match column type");
      expr_pb->ref_value(VERIFY_RESULT(value->Eval()));
    }
    values->push_back(dockv::KeyEntryValue::FromQLValuePB(
        expr_pb->value(), column.desc().sorting_type()));
  }

  SCHECK_EQ(
      hashed_components.size(), num_hash_keys,
      Corruption, "Number of hashed components does not match column description");
  SCHECK_EQ(
      range_components.size(), num_range_keys,
      Corruption, "Number of range components does not match column description");
  if (!hashed_values.empty()) {
    doc_key_.set_hash(VERIFY_RESULT(
        target_desc->partition_schema().PgsqlHashColumnCompoundValue(hashed_values)));
  }
  VLOG(5) << "Built ybctid: " << doc_key_.ToString();
  return doc_key_.Encode();
}

//--------------------------------------------------------------------------------------------------

PgApiImpl::PgApiImpl(
    PgApiContext context, const YBCPgTypeEntity *YBCDataTypeArray, int count,
    YBCPgCallbacks callbacks, std::optional<uint64_t> session_id,
    const YBCPgAshConfig* ash_config)
    : metric_registry_(std::move(context.metric_registry)),
      metric_entity_(std::move(context.metric_entity)),
      mem_tracker_(std::move(context.mem_tracker)),
      messenger_holder_(std::move(context.messenger_holder)),
      interrupter_(new Interrupter(messenger_holder_.messenger.get())),
      proxy_cache_(std::move(context.proxy_cache)),
      clock_(new server::HybridClock()),
      tserver_shared_object_(BuildTServerSharedObject()),
      pg_callbacks_(callbacks),
      pg_txn_manager_(new PgTxnManager(&pg_client_, clock_, pg_callbacks_)) {
  CHECK_OK(interrupter_->Start());
  CHECK_OK(clock_->Init());

  // Setup type mapping.
  for (int idx = 0; idx < count; idx++) {
    const YBCPgTypeEntity *type_entity = &YBCDataTypeArray[idx];
    type_map_[type_entity->type_oid] = type_entity;
  }

  CHECK_OK(pg_client_.Start(
      proxy_cache_.get(), &messenger_holder_.messenger->scheduler(),
      tserver_shared_object_, session_id, ash_config));
}

PgApiImpl::~PgApiImpl() {
  mem_contexts_.clear();
  pg_session_.reset();
  interrupter_.reset();
  pg_txn_manager_.reset();
  pg_client_.Shutdown();
}

void PgApiImpl::Interrupt() {
  interrupter_->Interrupt();
}

const YBCPgTypeEntity *PgApiImpl::FindTypeEntity(int type_oid) {
  const auto iter = type_map_.find(type_oid);
  if (iter != type_map_.end()) {
    return iter->second;
  }
  return nullptr;
}

//--------------------------------------------------------------------------------------------------

Status PgApiImpl::InitSession(const string& database_name, YBCPgExecStatsState* session_stats) {
  CHECK(!pg_session_);
  auto session = make_scoped_refptr<PgSession>(
      &pg_client_, database_name, pg_txn_manager_, pg_callbacks_, session_stats);
  if (!database_name.empty()) {
    RETURN_NOT_OK(session->ConnectDatabase(database_name));
  }

  pg_session_.swap(session);
  return Status::OK();
}

uint64_t PgApiImpl::GetSessionID() const { return pg_client_.SessionID(); }

Status PgApiImpl::InvalidateCache() {
  pg_session_->InvalidateAllTablesCache();
  return Status::OK();
}

bool PgApiImpl::GetDisableTransparentCacheRefreshRetry() {
  return FLAGS_TEST_ysql_disable_transparent_cache_refresh_retry;
}

//--------------------------------------------------------------------------------------------------

PgMemctx *PgApiImpl::CreateMemctx() {
  // Postgres will create YB Memctx when it first use the Memctx to allocate YugaByte object.
  return mem_contexts_.insert(std::make_unique<PgMemctx>()).first->get();
}

Status PgApiImpl::DestroyMemctx(PgMemctx *memctx) {
  // Postgres will destroy YB Memctx by releasing the pointer.
  auto it = mem_contexts_.find(memctx);
  SCHECK(it != mem_contexts_.end(), InternalError, "Invalid memory context handle");
  mem_contexts_.erase(it);
  return Status::OK();
}

Status PgApiImpl::ResetMemctx(PgMemctx *memctx) {
  // Postgres reset YB Memctx when clearing a context content without clearing its nested context.
  auto it = mem_contexts_.find(memctx);
  SCHECK(it != mem_contexts_.end(), InternalError, "Invalid memory context handle");
  (**it).Clear();
  return Status::OK();
}

// TODO(neil) Use Arena in the future.
// - PgStatement should have been declared as derived class of "MCBase".
// - All objects of PgStatement's derived class should be allocated by YbPgMemctx::Arena.
// - We cannot use Arena yet because quite a large number of YugaByte objects are being referenced
//   from other layers.  Those added code violated the original design as they assume ScopedPtr
//   instead of memory pool is being used. This mess should be cleaned up later.
//
// For now, statements is allocated as ScopedPtr and cached in the memory context. The statements
// would then be destructed when the context is destroyed and all other references are also cleared.
Status PgApiImpl::AddToCurrentPgMemctx(std::unique_ptr<PgStatement> stmt,
                                       PgStatement **handle) {
  *handle = stmt.get();
  pg_callbacks_.GetCurrentYbMemctx()->Register(stmt.release());
  return Status::OK();
}

// TODO(tvesely): Figure out how to use an arena for this
//
// For now, functions are allocated as ScopedPtr and cached in the memory context. The statements
// would then be destructed when the context is destroyed and all other references are also cleared.
Status PgApiImpl::AddToCurrentPgMemctx(std::unique_ptr<PgFunction> func, PgFunction **handle) {
  *handle = func.get();
  pg_callbacks_.GetCurrentYbMemctx()->Register(func.release());
  return Status::OK();
}

// TODO(neil) Most like we don't need table_desc. If we do need it, use Arena here.
// - PgTableDesc should have been declared as derived class of "MCBase".
// - PgTableDesc objects should be allocated by YbPgMemctx::Arena.
//
// For now, table_desc is allocated as ScopedPtr and cached in the memory context. The table_desc
// would then be destructed when the context is destroyed.
Status PgApiImpl::AddToCurrentPgMemctx(size_t table_desc_id,
                                       const PgTableDescPtr &table_desc) {
  pg_callbacks_.GetCurrentYbMemctx()->Cache(table_desc_id, table_desc);
  return Status::OK();
}

Status PgApiImpl::GetTabledescFromCurrentPgMemctx(size_t table_desc_id, PgTableDesc **handle) {
  pg_callbacks_.GetCurrentYbMemctx()->GetCache(table_desc_id, handle);
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status PgApiImpl::CreateSequencesDataTable() {
  return pg_session_->CreateSequencesDataTable();
}

Status PgApiImpl::InsertSequenceTuple(int64_t db_oid,
                                      int64_t seq_oid,
                                      uint64_t ysql_catalog_version,
                                      bool is_db_catalog_version_mode,
                                      int64_t last_val,
                                      bool is_called) {
  return pg_session_->InsertSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, is_db_catalog_version_mode, last_val, is_called);
}

Status PgApiImpl::UpdateSequenceTupleConditionally(int64_t db_oid,
                                                   int64_t seq_oid,
                                                   uint64_t ysql_catalog_version,
                                                   bool is_db_catalog_version_mode,
                                                   int64_t last_val,
                                                   bool is_called,
                                                   int64_t expected_last_val,
                                                   bool expected_is_called,
                                                   bool *skipped) {
  *skipped = VERIFY_RESULT(pg_session_->UpdateSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, is_db_catalog_version_mode, last_val, is_called,
      expected_last_val, expected_is_called));
  return Status::OK();
}

Status PgApiImpl::UpdateSequenceTuple(int64_t db_oid,
                                      int64_t seq_oid,
                                      uint64_t ysql_catalog_version,
                                      bool is_db_catalog_version_mode,
                                      int64_t last_val,
                                      bool is_called,
                                      bool* skipped) {
  bool result = VERIFY_RESULT(pg_session_->UpdateSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, is_db_catalog_version_mode, last_val,
      is_called, std::nullopt, std::nullopt));
  if (skipped) {
    *skipped = result;
  }
  return Status::OK();
}

Status PgApiImpl::FetchSequenceTuple(int64_t db_oid,
                                     int64_t seq_oid,
                                     uint64_t ysql_catalog_version,
                                     bool is_db_catalog_version_mode,
                                     uint32_t fetch_count,
                                     int64_t inc_by,
                                     int64_t min_value,
                                     int64_t max_value,
                                     bool cycle,
                                     int64_t *first_value,
                                     int64_t *last_value) {
  auto res = VERIFY_RESULT(pg_session_->FetchSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, is_db_catalog_version_mode, fetch_count, inc_by,
      min_value, max_value, cycle));
  *first_value = res.first;
  *last_value = res.second;
  return Status::OK();
}

Status PgApiImpl::ReadSequenceTuple(int64_t db_oid,
                                    int64_t seq_oid,
                                    uint64_t ysql_catalog_version,
                                    bool is_db_catalog_version_mode,
                                    int64_t *last_val,
                                    bool *is_called) {
  auto res = VERIFY_RESULT(pg_session_->ReadSequenceTuple(
    db_oid, seq_oid, ysql_catalog_version, is_db_catalog_version_mode));
  if (last_val) {
    *last_val = res.first;
  }
  if (is_called) {
    *is_called = res.second;
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

void PgApiImpl::DeleteStatement(PgStatement *handle) {
  if (handle) {
    PgMemctx::Destroy(handle);
  }
}

//--------------------------------------------------------------------------------------------------

Status PgApiImpl::ConnectDatabase(const char *database_name) {
  return pg_session_->ConnectDatabase(database_name);
}

Status PgApiImpl::IsDatabaseColocated(const PgOid database_oid, bool *colocated,
                                      bool *legacy_colocated_database) {
  return pg_session_->IsDatabaseColocated(database_oid, colocated, legacy_colocated_database);
}

Status PgApiImpl::NewCreateDatabase(
    const char* database_name, const PgOid database_oid, const PgOid source_database_oid,
    const PgOid next_oid, const bool colocated, YbCloneInfo *yb_clone_info, PgStatement** handle) {
  auto stmt = std::make_unique<PgCreateDatabase>(
      pg_session_, database_name, database_oid, source_database_oid, next_oid,
      yb_clone_info, colocated);
  if (pg_txn_manager_->IsDdlMode()) {
    stmt->UseTransaction();
  }
  RETURN_NOT_OK(AddToCurrentPgMemctx(std::move(stmt), handle));
  return Status::OK();
}

Status PgApiImpl::ExecCreateDatabase(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_CREATE_DATABASE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  return down_cast<PgCreateDatabase*>(handle)->Exec();
}

Status PgApiImpl::NewDropDatabase(const char *database_name,
                                  PgOid database_oid,
                                  PgStatement **handle) {
  auto stmt = std::make_unique<PgDropDatabase>(pg_session_, database_name, database_oid);
  RETURN_NOT_OK(AddToCurrentPgMemctx(std::move(stmt), handle));
  return Status::OK();
}

Status PgApiImpl::ExecDropDatabase(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_DROP_DATABASE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return down_cast<PgDropDatabase*>(handle)->Exec();
}

Status PgApiImpl::NewAlterDatabase(const char *database_name,
                                  PgOid database_oid,
                                  PgStatement **handle) {
  auto stmt = std::make_unique<PgAlterDatabase>(pg_session_, database_name, database_oid);
  RETURN_NOT_OK(AddToCurrentPgMemctx(std::move(stmt), handle));
  return Status::OK();
}

Status PgApiImpl::AlterDatabaseRenameDatabase(PgStatement *handle, const char *newname) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_ALTER_DATABASE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  down_cast<PgAlterDatabase*>(handle)->RenameDatabase(newname);
  return Status::OK();
}

Status PgApiImpl::ExecAlterDatabase(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_ALTER_DATABASE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return down_cast<PgAlterDatabase*>(handle)->Exec();
}

Status PgApiImpl::ReserveOids(const PgOid database_oid,
                              const PgOid next_oid,
                              const uint32_t count,
                              PgOid *begin_oid,
                              PgOid *end_oid) {
  auto p = VERIFY_RESULT(pg_client_.ReserveOids(database_oid, next_oid, count));
  *begin_oid = p.first;
  *end_oid = p.second;
  return Status::OK();
}

Status PgApiImpl::GetNewObjectId(PgOid db_oid, PgOid* new_oid) {
  auto result = VERIFY_RESULT(pg_client_.GetNewObjectId(db_oid));
  *new_oid = result;
  return Status::OK();
}

Status PgApiImpl::GetCatalogMasterVersion(uint64_t *version) {
  return pg_session_->GetCatalogMasterVersion(version);
}

Status PgApiImpl::CancelTransaction(const unsigned char* transaction_id) {
  return pg_session_->CancelTransaction(transaction_id);
}

Result<PgTableDescPtr> PgApiImpl::LoadTable(const PgObjectId& table_id) {
  return pg_session_->LoadTable(table_id);
}

void PgApiImpl::InvalidateTableCache(const PgObjectId& table_id) {
  pg_session_->InvalidateTableCache(table_id, InvalidateOnPgClient::kTrue);
}

//--------------------------------------------------------------------------------------------------

Status PgApiImpl::NewCreateTablegroup(const char *database_name,
                                      const PgOid database_oid,
                                      const PgOid tablegroup_oid,
                                      const PgOid tablespace_oid,
                                      PgStatement **handle) {
  SCHECK(pg_txn_manager_->IsDdlMode(),
         IllegalState,
         "Tablegroup is being created outside of DDL mode");
  auto stmt = std::make_unique<PgCreateTablegroup>(pg_session_, database_name,
                                                   database_oid, tablegroup_oid, tablespace_oid);
  RETURN_NOT_OK(AddToCurrentPgMemctx(std::move(stmt), handle));
  return Status::OK();
}

Status PgApiImpl::ExecCreateTablegroup(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_CREATE_TABLEGROUP)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  pg_session_->SetDdlHasSyscatalogChanges();
  return down_cast<PgCreateTablegroup*>(handle)->Exec();
}

Status PgApiImpl::NewDropTablegroup(const PgOid database_oid,
                                    const PgOid tablegroup_oid,
                                    PgStatement **handle) {
  SCHECK(pg_txn_manager_->IsDdlMode(),
         IllegalState,
         "Tablegroup is being dropped outside of DDL mode");
  auto stmt = std::make_unique<PgDropTablegroup>(pg_session_, database_oid, tablegroup_oid);
  RETURN_NOT_OK(AddToCurrentPgMemctx(std::move(stmt), handle));
  return Status::OK();
}


Status PgApiImpl::ExecDropTablegroup(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_DROP_TABLEGROUP)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  pg_session_->SetDdlHasSyscatalogChanges();
  return down_cast<PgDropTablegroup*>(handle)->Exec();
}


//--------------------------------------------------------------------------------------------------

Status PgApiImpl::NewCreateTable(const char *database_name,
                                 const char *schema_name,
                                 const char *table_name,
                                 const PgObjectId& table_id,
                                 bool is_shared_table,
                                 bool is_sys_catalog_table,
                                 bool if_not_exist,
                                 PgYbrowidMode ybrowid_mode,
                                 bool is_colocated_via_database,
                                 const PgObjectId& tablegroup_oid,
                                 const ColocationId colocation_id,
                                 const PgObjectId& tablespace_oid,
                                 bool is_matview,
                                 const PgObjectId& pg_table_oid,
                                 const PgObjectId& old_relfilenode_oid,
                                 bool is_truncate,
                                 PgStatement **handle) {
  auto stmt = std::make_unique<PgCreateTable>(
      pg_session_, database_name, schema_name, table_name, table_id, is_shared_table,
      is_sys_catalog_table, if_not_exist, ybrowid_mode, is_colocated_via_database,
      tablegroup_oid, colocation_id, tablespace_oid, is_matview, pg_table_oid,
      old_relfilenode_oid, is_truncate);
  if (pg_txn_manager_->IsDdlMode()) {
    stmt->UseTransaction();
  }
  RETURN_NOT_OK(AddToCurrentPgMemctx(std::move(stmt), handle));
  return Status::OK();
}

Status PgApiImpl::CreateTableAddColumn(PgStatement *handle, const char *attr_name, int attr_num,
                                       const YBCPgTypeEntity *attr_type,
                                       bool is_hash, bool is_range,
                                       bool is_desc, bool is_nulls_first) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_CREATE_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return AddColumn(down_cast<PgCreateTable*>(handle), attr_name, attr_num, attr_type,
      is_hash, is_range, is_desc, is_nulls_first);
}

Status PgApiImpl::CreateTableSetNumTablets(PgStatement *handle, int32_t num_tablets) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_CREATE_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return down_cast<PgCreateTable*>(handle)->SetNumTablets(num_tablets);
}

Status PgApiImpl::AddSplitBoundary(PgStatement *handle, PgExpr **exprs, int expr_count) {
  // Partitioning a TABLE or an INDEX.
  if (PgStatement::IsValidStmt(handle, StmtOp::STMT_CREATE_TABLE) ||
      PgStatement::IsValidStmt(handle, StmtOp::STMT_CREATE_INDEX)) {
    return down_cast<PgCreateTable*>(handle)->AddSplitBoundary(exprs, expr_count);
  }

  // Invalid handle.
  return STATUS(InvalidArgument, "Invalid statement handle");
}

Status PgApiImpl::ExecCreateTable(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_CREATE_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  pg_session_->SetDdlHasSyscatalogChanges();
  return down_cast<PgCreateTable*>(handle)->Exec();
}

Status PgApiImpl::NewAlterTable(const PgObjectId& table_id,
                                PgStatement **handle) {
  auto stmt = std::make_unique<PgAlterTable>(pg_session_, table_id);
  if (pg_txn_manager_->IsDdlMode()) {
    stmt->UseTransaction();
  }
  RETURN_NOT_OK(AddToCurrentPgMemctx(std::move(stmt), handle));
  return Status::OK();
}

Status PgApiImpl::AlterTableAddColumn(PgStatement *handle, const char *name,
                                      int order,
                                      const YBCPgTypeEntity *attr_type,
                                      YBCPgExpr missing_value) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_ALTER_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgAlterTable *pg_stmt = down_cast<PgAlterTable*>(handle);
  return pg_stmt->AddColumn(name, attr_type, order, missing_value);
}

Status PgApiImpl::AlterTableRenameColumn(PgStatement *handle, const char *oldname,
                                         const char *newname) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_ALTER_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgAlterTable *pg_stmt = down_cast<PgAlterTable*>(handle);
  return pg_stmt->RenameColumn(oldname, newname);
}

Status PgApiImpl::AlterTableDropColumn(PgStatement *handle, const char *name) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_ALTER_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgAlterTable *pg_stmt = down_cast<PgAlterTable*>(handle);
  return pg_stmt->DropColumn(name);
}

Status PgApiImpl::AlterTableSetReplicaIdentity(PgStatement *handle, const char identity_type) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_ALTER_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgAlterTable *pg_stmt = down_cast<PgAlterTable*>(handle);
  return pg_stmt->SetReplicaIdentity(identity_type);
}

Status PgApiImpl::AlterTableRenameTable(PgStatement *handle, const char *db_name,
                                        const char *newname) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_ALTER_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgAlterTable *pg_stmt = down_cast<PgAlterTable*>(handle);
  return pg_stmt->RenameTable(db_name, newname);
}

Status PgApiImpl::AlterTableIncrementSchemaVersion(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_ALTER_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgAlterTable *pg_stmt = down_cast<PgAlterTable*>(handle);
  return pg_stmt->IncrementSchemaVersion();
}

Status PgApiImpl::AlterTableSetTableId(PgStatement *handle, const PgObjectId &table_id) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_ALTER_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgAlterTable *pg_stmt = down_cast<PgAlterTable *>(handle);
  return pg_stmt->SetTableId(table_id);
}

Status PgApiImpl::AlterTableSetSchema(PgStatement *handle, const char *schema_name) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_ALTER_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgAlterTable *pg_stmt = down_cast<PgAlterTable*>(handle);
  return pg_stmt->SetSchema(schema_name);
}

Status PgApiImpl::ExecAlterTable(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_ALTER_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  pg_session_->SetDdlHasSyscatalogChanges();
  PgAlterTable *pg_stmt = down_cast<PgAlterTable*>(handle);
  return pg_stmt->Exec();
}

Status PgApiImpl::AlterTableInvalidateTableCacheEntry(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_ALTER_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  PgAlterTable *pg_stmt = down_cast<PgAlterTable*>(handle);
  pg_stmt->InvalidateTableCacheEntry();
  return Status::OK();
}

Status PgApiImpl::NewDropTable(const PgObjectId& table_id,
                               bool if_exist,
                               PgStatement **handle) {
  SCHECK(pg_txn_manager_->IsDdlMode(),
         IllegalState,
         "Table is being dropped outside of DDL mode");
  auto stmt = std::make_unique<PgDropTable>(pg_session_, table_id, if_exist);
  RETURN_NOT_OK(AddToCurrentPgMemctx(std::move(stmt), handle));
  return Status::OK();
}

Status PgApiImpl::NewTruncateTable(const PgObjectId& table_id,
                                   PgStatement **handle) {
  auto stmt = std::make_unique<PgTruncateTable>(pg_session_, table_id);
  RETURN_NOT_OK(AddToCurrentPgMemctx(std::move(stmt), handle));
  return Status::OK();
}

Status PgApiImpl::ExecTruncateTable(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_TRUNCATE_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return down_cast<PgTruncateTable*>(handle)->Exec();
}

Status PgApiImpl::NewDropSequence(const YBCPgOid database_oid,
                                  const YBCPgOid sequence_oid,
                                  PgStatement **handle) {
  auto stmt = std::make_unique<PgDropSequence>(pg_session_, database_oid,
      sequence_oid);
  RETURN_NOT_OK(AddToCurrentPgMemctx(std::move(stmt), handle));
  return Status::OK();
}

Status PgApiImpl::ExecDropSequence(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_DROP_SEQUENCE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  PgDropSequence *pg_stmt = down_cast<PgDropSequence*>(handle);
  return pg_stmt->Exec();
}

Status PgApiImpl::NewDropDBSequences(const YBCPgOid database_oid,
                                     PgStatement **handle) {
  auto stmt = std::make_unique<PgDropDBSequences>(pg_session_, database_oid);
  RETURN_NOT_OK(AddToCurrentPgMemctx(std::move(stmt), handle));
  return Status::OK();
}

Status PgApiImpl::GetTableDesc(const PgObjectId& table_id,
                               PgTableDesc **handle) {
  // First read from memory context.
  size_t hash_id = hash_value(table_id);
  RETURN_NOT_OK(GetTabledescFromCurrentPgMemctx(hash_id, handle));

  // Read from environment.
  if (*handle == nullptr) {
    auto result = pg_session_->LoadTable(table_id);
    RETURN_NOT_OK(result);
    RETURN_NOT_OK(AddToCurrentPgMemctx(hash_id, *result));

    *handle = result->get();
  }

  return Status::OK();
}

Result<YBCPgColumnInfo> PgApiImpl::GetColumnInfo(YBCPgTableDesc table_desc,
                                                 int16_t attr_number) {
  return table_desc->GetColumnInfo(attr_number);
}

Status PgApiImpl::DmlModifiesRow(PgStatement *handle, bool *modifies_row) {
  if (!handle) {
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  *modifies_row = false;

  switch (handle->stmt_op()) {
    case StmtOp::STMT_UPDATE:
    case StmtOp::STMT_DELETE:
      *modifies_row = true;
      break;
    default:
      break;
  }

  return Status::OK();
}

Status PgApiImpl::SetIsSysCatalogVersionChange(PgStatement *handle) {
  if (!handle) {
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  switch (handle->stmt_op()) {
    case StmtOp::STMT_UPDATE:
    case StmtOp::STMT_DELETE:
    case StmtOp::STMT_INSERT:
      down_cast<PgDmlWrite *>(handle)->SetIsSystemCatalogChange();
      return Status::OK();
    default:
      break;
  }

  return STATUS(InvalidArgument, "Invalid statement handle");
}

Status PgApiImpl::SetCatalogCacheVersion(
  PgStatement *handle, uint64_t version, std::optional<PgOid> db_oid) {
  if (!handle) {
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  switch (handle->stmt_op()) {
    case StmtOp::STMT_SELECT:
    case StmtOp::STMT_INSERT:
    case StmtOp::STMT_UPDATE:
    case StmtOp::STMT_DELETE:
      down_cast<PgDml*>(handle)->SetCatalogCacheVersion(db_oid, version);
      return Status::OK();
    default:
      break;
  }

  return STATUS(InvalidArgument, "Invalid statement handle");
}

Result<client::TableSizeInfo> PgApiImpl::GetTableDiskSize(const PgObjectId& table_oid) {
  return pg_session_->GetTableDiskSize(table_oid);
}

//--------------------------------------------------------------------------------------------------

Status PgApiImpl::NewCreateIndex(const char *database_name,
                                 const char *schema_name,
                                 const char *index_name,
                                 const PgObjectId& index_id,
                                 const PgObjectId& base_table_id,
                                 bool is_shared_index,
                                 bool is_sys_catalog_index,
                                 bool is_unique_index,
                                 const bool skip_index_backfill,
                                 bool if_not_exist,
                                 bool is_colocated_via_database,
                                 const PgObjectId& tablegroup_oid,
                                 const YBCPgOid& colocation_id,
                                 const PgObjectId& tablespace_oid,
                                 const PgObjectId& pg_table_id,
                                 const PgObjectId& old_relfilenode_id,
                                 PgStatement **handle) {
  auto stmt = std::make_unique<PgCreateTable>(
      pg_session_, database_name, schema_name, index_name, index_id, is_shared_index,
      is_sys_catalog_index, if_not_exist, PG_YBROWID_MODE_NONE,
      is_colocated_via_database, tablegroup_oid, colocation_id,
      tablespace_oid, false /* is_matview */, pg_table_id, old_relfilenode_id,
      false /* is_truncate */);
  stmt->SetupIndex(base_table_id, is_unique_index, skip_index_backfill);
  if (pg_txn_manager_->IsDdlMode()) {
      stmt->UseTransaction();
  }
  RETURN_NOT_OK(AddToCurrentPgMemctx(std::move(stmt), handle));
  return Status::OK();
}

Status PgApiImpl::CreateIndexAddColumn(PgStatement *handle, const char *attr_name, int attr_num,
                                       const YBCPgTypeEntity *attr_type,
                                       bool is_hash, bool is_range,
                                       bool is_desc, bool is_nulls_first) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_CREATE_INDEX)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  return AddColumn(down_cast<PgCreateTable*>(handle), attr_name, attr_num, attr_type,
      is_hash, is_range, is_desc, is_nulls_first);
}

Status PgApiImpl::CreateIndexSetNumTablets(PgStatement *handle, int32_t num_tablets) {
  SCHECK(PgStatement::IsValidStmt(handle, StmtOp::STMT_CREATE_INDEX),
         InvalidArgument,
         "Invalid statement handle");
  return down_cast<PgCreateTable*>(handle)->SetNumTablets(num_tablets);
}

Status PgApiImpl::CreateIndexSetVectorOptions(PgStatement *handle, YbPgVectorIdxOptions *options) {
  SCHECK(PgStatement::IsValidStmt(handle, StmtOp::STMT_CREATE_INDEX),
         InvalidArgument,
         "Invalid statement handle");
  return down_cast<PgCreateTable*>(handle)->SetVectorOptions(options);
}

Status PgApiImpl::ExecCreateIndex(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_CREATE_INDEX)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  pg_session_->SetDdlHasSyscatalogChanges();
  return down_cast<PgCreateTable*>(handle)->Exec();
}

Status PgApiImpl::NewDropIndex(const PgObjectId& index_id,
                               bool if_exist,
                               bool ddl_rollback_enabled,
                               PgStatement **handle) {
  SCHECK(pg_txn_manager_->IsDdlMode(),
         IllegalState,
         "Index is being dropped outside of DDL mode");
  auto stmt = std::make_unique<PgDropIndex>(pg_session_, index_id, if_exist, ddl_rollback_enabled);
  RETURN_NOT_OK(AddToCurrentPgMemctx(std::move(stmt), handle));
  return Status::OK();
}

Status PgApiImpl::ExecPostponedDdlStmt(PgStatement *handle) {
  if (!handle) {
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  switch (handle->stmt_op()) {
    case StmtOp::STMT_DROP_TABLE:
      return down_cast<PgDropTable*>(handle)->Exec();
    case StmtOp::STMT_DROP_INDEX:
      return down_cast<PgDropIndex*>(handle)->Exec();
    case StmtOp::STMT_DROP_TABLEGROUP:
      return down_cast<PgDropTablegroup*>(handle)->Exec();
    case StmtOp::STMT_DROP_SEQUENCE:
      return down_cast<PgDropSequence*>(handle)->Exec();
    case StmtOp::STMT_DROP_DB_SEQUENCES:
      return down_cast<PgDropDBSequences*>(handle)->Exec();

    default:
      break;
  }
  return STATUS(InvalidArgument, "Invalid statement handle");
}

Status PgApiImpl::ExecDropTable(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_DROP_TABLE)) {
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  pg_session_->SetDdlHasSyscatalogChanges();
  return down_cast<PgDropTable*>(handle)->Exec();
}

Status PgApiImpl::ExecDropIndex(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_DROP_INDEX)) {
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  pg_session_->SetDdlHasSyscatalogChanges();
  return down_cast<PgDropIndex*>(handle)->Exec();
}

Result<int> PgApiImpl::WaitForBackendsCatalogVersion(PgOid dboid, uint64_t version) {
  tserver::PgWaitForBackendsCatalogVersionRequestPB req;
  req.set_database_oid(dboid);
  req.set_catalog_version(version);
  // Incorporate the margin into the deadline because master will subtract the margin for
  // responding.
  return pg_session_->pg_client().WaitForBackendsCatalogVersion(
      &req,
      CoarseMonoClock::Now() + MonoDelta::FromMilliseconds(
        FLAGS_wait_for_ysql_backends_catalog_version_client_master_rpc_timeout_ms
        + FLAGS_wait_for_ysql_backends_catalog_version_client_master_rpc_margin_ms));
}

Status PgApiImpl::BackfillIndex(const PgObjectId& table_id) {
  tserver::PgBackfillIndexRequestPB req;
  table_id.ToPB(req.mutable_table_id());
  return pg_session_->pg_client().BackfillIndex(
      &req, CoarseMonoClock::Now() + FLAGS_backfill_index_client_rpc_timeout_ms * 1ms);
}

//--------------------------------------------------------------------------------------------------
// DML Statement Support.
//--------------------------------------------------------------------------------------------------

// Binding -----------------------------------------------------------------------------------------

Status PgApiImpl::DmlAppendTarget(PgStatement *handle, PgExpr *target) {
  return down_cast<PgDml*>(handle)->AppendTarget(target);
}

Status PgApiImpl::DmlAppendQual(PgStatement *handle, PgExpr *qual, bool is_primary) {
  return down_cast<PgDml*>(handle)->AppendQual(qual, is_primary);
}

Status PgApiImpl::DmlAppendColumnRef(PgStatement *handle, PgColumnRef *colref, bool is_primary) {
  return down_cast<PgDml*>(handle)->AppendColumnRef(colref, is_primary);
}

Status PgApiImpl::DmlBindColumn(PgStatement *handle, int attr_num, PgExpr *attr_value) {
  return down_cast<PgDml*>(handle)->BindColumn(attr_num, attr_value);
}

Status PgApiImpl::DmlBindRow(
    YBCPgStatement handle, uint64_t ybctid, YBCBindColumn* columns, int count) {
  return down_cast<PgDmlWrite*>(handle)->BindRow(ybctid, columns, count);
}

Status PgApiImpl::DmlBindColumnCondBetween(PgStatement *handle, int attr_num,
                                           PgExpr *attr_value,
                                           bool start_inclusive,
                                           PgExpr *attr_value_end,
                                           bool end_inclusive) {
  return down_cast<PgDmlRead*>(handle)->BindColumnCondBetween(attr_num,
                                                              attr_value,
                                                              start_inclusive,
                                                              attr_value_end,
                                                              end_inclusive);
}

Status PgApiImpl::DmlBindColumnCondIsNotNull(PgStatement *handle, int attr_num) {
  return down_cast<PgDmlRead*>(handle)->BindColumnCondIsNotNull(attr_num);
}

Status PgApiImpl::DmlBindColumnCondIn(PgStatement *handle, YBCPgExpr lhs, int n_attr_values,
                                      PgExpr **attr_values) {
  return down_cast<PgDmlRead*>(handle)->BindColumnCondIn(lhs, n_attr_values, attr_values);
}

Status PgApiImpl::DmlAddRowUpperBound(YBCPgStatement handle,
    int n_col_values, PgExpr **col_values, bool is_inclusive) {
    return down_cast<PgDmlRead*>(handle)->AddRowUpperBound(handle,
                                                        n_col_values,
                                                        col_values,
                                                        is_inclusive);
}

Status PgApiImpl::DmlAddRowLowerBound(YBCPgStatement handle,
    int n_col_values, PgExpr **col_values, bool is_inclusive) {
    return down_cast<PgDmlRead*>(handle)->AddRowLowerBound(handle,
                                                        n_col_values,
                                                        col_values,
                                                        is_inclusive);
}

void PgApiImpl::DmlBindHashCode(
  PgStatement* handle, const std::optional<Bound>& start, const std::optional<Bound>& end) {
  down_cast<PgDmlRead*>(handle)->BindHashCode(start, end);
}

Status PgApiImpl::DmlBindRange(YBCPgStatement handle,
                               Slice lower_bound,
                               bool lower_bound_inclusive,
                               Slice upper_bound,
                               bool upper_bound_inclusive) {
  return down_cast<PgDmlRead*>(handle)->BindRange(
      lower_bound, lower_bound_inclusive, upper_bound, upper_bound_inclusive);
}

Status PgApiImpl::DmlBindTable(PgStatement *handle) {
  return down_cast<PgDml*>(handle)->BindTable();
}

Result<YBCPgColumnInfo> PgApiImpl::DmlGetColumnInfo(YBCPgStatement handle, int attr_num) {
  return down_cast<PgDml*>(handle)->GetColumnInfo(attr_num);
}

Status PgApiImpl::DmlAssignColumn(PgStatement *handle, int attr_num, PgExpr *attr_value) {
  return down_cast<PgDml*>(handle)->AssignColumn(attr_num, attr_value);
}

Status PgApiImpl::DmlFetch(PgStatement *handle, int32_t natts, uint64_t *values, bool *isnulls,
                           PgSysColumns *syscols, bool *has_data) {
  return down_cast<PgDml*>(handle)->Fetch(natts, values, isnulls, syscols, has_data);
}

Result<dockv::KeyBytes> PgApiImpl::BuildTupleId(const YBCPgYBTupleIdDescriptor& descr) {
    return tuple_id_builder_.Build(pg_session_.get(), descr);
}

Status PgApiImpl::StartOperationsBuffering() {
  return pg_session_->StartOperationsBuffering();
}

Status PgApiImpl::StopOperationsBuffering() {
  return pg_session_->StopOperationsBuffering();
}

void PgApiImpl::ResetOperationsBuffering() {
  pg_session_->ResetOperationsBuffering();
}

Status PgApiImpl::FlushBufferedOperations() {
  return pg_session_->FlushBufferedOperations();
}

Status PgApiImpl::DmlExecWriteOp(PgStatement *handle, int32_t *rows_affected_count) {
  switch (handle->stmt_op()) {
    case StmtOp::STMT_INSERT:
    case StmtOp::STMT_UPDATE:
    case StmtOp::STMT_DELETE:
    case StmtOp::STMT_TRUNCATE:
      {
        auto dml_write = down_cast<PgDmlWrite *>(handle);
        RETURN_NOT_OK(dml_write->Exec(ForceNonBufferable(rows_affected_count != nullptr)));
        if (rows_affected_count) {
          *rows_affected_count = dml_write->GetRowsAffectedCount();
        }
        return Status::OK();
      }
    default:
      break;
  }
  return STATUS(InvalidArgument, "Invalid statement handle");
}

// Insert ------------------------------------------------------------------------------------------

Result<PgStatement*> PgApiImpl::NewInsertBlock(
    const PgObjectId& table_id,
    bool is_region_local,
    YBCPgTransactionSetting transaction_setting) {
  if (!FLAGS_ysql_pack_inserted_value) {
    return nullptr;
  }

  auto stmt = std::make_unique<PgInsert>(
      pg_session_, table_id, is_region_local, transaction_setting, /* packed= */ true);
  RETURN_NOT_OK(stmt->Prepare());
  PgStatement *result = nullptr;
  RETURN_NOT_OK(AddToCurrentPgMemctx(std::move(stmt), &result));
  return result;
}

Status PgApiImpl::NewInsert(const PgObjectId& table_id,
                            bool is_region_local,
                            PgStatement **handle,
                            YBCPgTransactionSetting transaction_setting,
                            PgStatement *block_insert_stmt) {
  *handle = nullptr;
  auto stmt = std::make_unique<PgInsert>(
      pg_session_, table_id, is_region_local, transaction_setting, /* packed= */ false);
  RETURN_NOT_OK(stmt->Prepare());
  RETURN_NOT_OK(AddToCurrentPgMemctx(std::move(stmt), handle));
  return Status::OK();
}

Status PgApiImpl::ExecInsert(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_INSERT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return down_cast<PgInsert*>(handle)->Exec();
}

Status PgApiImpl::InsertStmtSetUpsertMode(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_INSERT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  down_cast<PgInsert*>(handle)->SetUpsertMode();

  return Status::OK();
}

Status PgApiImpl::InsertStmtSetWriteTime(PgStatement *handle, const HybridTime write_time) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_INSERT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  RETURN_NOT_OK(down_cast<PgInsert*>(handle)->SetWriteTime(write_time));
  return Status::OK();
}

Status PgApiImpl::InsertStmtSetIsBackfill(PgStatement *handle, const bool is_backfill) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_INSERT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  down_cast<PgInsert*>(handle)->SetIsBackfill(is_backfill);
  return Status::OK();
}

// Update ------------------------------------------------------------------------------------------

Status PgApiImpl::NewUpdate(const PgObjectId& table_id,
                            bool is_region_local,
                            PgStatement **handle,
                            YBCPgTransactionSetting transaction_setting) {
  *handle = nullptr;
  auto stmt = std::make_unique<PgUpdate>(
      pg_session_, table_id, is_region_local, transaction_setting);
  RETURN_NOT_OK(stmt->Prepare());
  RETURN_NOT_OK(AddToCurrentPgMemctx(std::move(stmt), handle));
  return Status::OK();
}

Status PgApiImpl::ExecUpdate(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_UPDATE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return down_cast<PgUpdate*>(handle)->Exec();
}

// Delete ------------------------------------------------------------------------------------------

Status PgApiImpl::NewDelete(const PgObjectId& table_id,
                            bool is_region_local,
                            PgStatement **handle,
                            YBCPgTransactionSetting transaction_setting) {
  *handle = nullptr;
  auto stmt = std::make_unique<PgDelete>(
      pg_session_, table_id, is_region_local, transaction_setting);
  RETURN_NOT_OK(stmt->Prepare());
  RETURN_NOT_OK(AddToCurrentPgMemctx(std::move(stmt), handle));
  return Status::OK();
}

Status PgApiImpl::ExecDelete(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_DELETE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return down_cast<PgDelete*>(handle)->Exec();
}

Status PgApiImpl::NewSample(const PgObjectId& table_id,
                            int targrows,
                            bool is_region_local,
                            PgStatement **handle) {
  *handle = nullptr;
  auto sample = std::make_unique<PgSample>(pg_session_, targrows, table_id, is_region_local);
  RETURN_NOT_OK(sample->Prepare());
  RETURN_NOT_OK(AddToCurrentPgMemctx(std::move(sample), handle));
  return Status::OK();
}

Status PgApiImpl::InitRandomState(
    PgStatement *handle, double rstate_w, uint64_t rand_state_s0, uint64_t rand_state_s1) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_SAMPLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  RETURN_NOT_OK(
      down_cast<PgSample *>(handle)->InitRandomState(rstate_w, rand_state_s0, rand_state_s1));
  return Status::OK();
}

Result<bool> PgApiImpl::SampleNextBlock(PgStatement* handle) {
  RSTATUS_DCHECK(
      PgStatement::IsValidStmt(handle, StmtOp::STMT_SAMPLE),
      InvalidArgument, "Invalid statement handle");
  return down_cast<PgSample*>(handle)->SampleNextBlock();
}

Status PgApiImpl::ExecSample(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_SAMPLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  RETURN_NOT_OK(down_cast<PgSample*>(handle)->Exec(nullptr));
  return Status::OK();
}

Result<EstimatedRowCount> PgApiImpl::GetEstimatedRowCount(PgStatement* handle) {
  RSTATUS_DCHECK(
      PgStatement::IsValidStmt(handle, StmtOp::STMT_SAMPLE),
      InvalidArgument, "Invalid statement handle");
  return down_cast<PgSample*>(handle)->GetEstimatedRowCount();
}

Status PgApiImpl::DeleteStmtSetIsPersistNeeded(PgStatement *handle, const bool is_persist_needed) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_DELETE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  down_cast<PgDelete*>(handle)->SetIsPersistNeeded(is_persist_needed);
  return Status::OK();
}

// Colocated Truncate ------------------------------------------------------------------------------

Status PgApiImpl::NewTruncateColocated(const PgObjectId& table_id,
                                       bool is_region_local,
                                       PgStatement **handle,
                                       YBCPgTransactionSetting transaction_setting) {
  *handle = nullptr;
  auto stmt = std::make_unique<PgTruncateColocated>(
      pg_session_, table_id, is_region_local, transaction_setting);
  RETURN_NOT_OK(stmt->Prepare());
  RETURN_NOT_OK(AddToCurrentPgMemctx(std::move(stmt), handle));
  return Status::OK();
}

Status PgApiImpl::ExecTruncateColocated(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_TRUNCATE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return down_cast<PgTruncateColocated*>(handle)->Exec();
}

// Select ------------------------------------------------------------------------------------------

Status PgApiImpl::NewSelect(
    const PgObjectId& table_id, const PgObjectId& index_id,
    const PgPrepareParameters* prepare_params, bool is_region_local, PgStatement** handle) {
  // Scenarios:
  // - Sequential Scan: PgSelect to read from table_id.
  // - Primary Scan: PgSelect from table_id. YugaByte does not have separate table for primary key.
  // - Index-Only-Scan: PgSelectIndex directly from secondary index_id.
  // - IndexScan: Use PgSelectIndex to read from index_id and then PgSelect to read from table_id.
  //     Note that for SysTable, only one request is send for both table_id and index_id.
  *handle = nullptr;
  std::unique_ptr<PgDmlRead> stmt;
  PgDml::PrepareParameters dml_prepare_params;
  if (prepare_params) {
    dml_prepare_params.querying_colocated_table = prepare_params->querying_colocated_table;
    dml_prepare_params.index_only_scan = prepare_params->index_only_scan;
    dml_prepare_params.fetch_ybctids_only = prepare_params->fetch_ybctids_only;
    if (prepare_params->index_only_scan && prepare_params->use_secondary_index) {
      RSTATUS_DCHECK(index_id.IsValid(), InvalidArgument, "Cannot run query with invalid index ID");
      stmt = std::make_unique<PgSelectIndex>(
          pg_session_, index_id, is_region_local, dml_prepare_params);
    }
  }

  if (!stmt) {
    stmt = std::make_unique<PgSelect>(
        pg_session_, table_id, is_region_local, dml_prepare_params, index_id);
  }
  RETURN_NOT_OK(stmt->Prepare());
  RETURN_NOT_OK(AddToCurrentPgMemctx(std::move(stmt), handle));
  return Status::OK();
}

Status PgApiImpl::SetForwardScan(PgStatement *handle, bool is_forward_scan) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_SELECT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  down_cast<PgDmlRead*>(handle)->SetForwardScan(is_forward_scan);
  return Status::OK();
}

Status PgApiImpl::SetDistinctPrefixLength(PgStatement *handle, int distinct_prefix_length) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_SELECT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  down_cast<PgDmlRead*>(handle)->SetDistinctPrefixLength(distinct_prefix_length);
  return Status::OK();
}

Status PgApiImpl::SetHashBounds(PgStatement *handle, uint16_t low_bound, uint16_t high_bound) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_SELECT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  down_cast<PgDmlRead*>(handle)->SetHashBounds(low_bound, high_bound);
  return Status::OK();
}

Slice PgApiImpl::GetYbctidAsSlice(uint64_t ybctid) {
  char* value = NULL;
  int64_t bytes = 0;
  FindTypeEntity(kByteArrayOid)->datum_to_yb(ybctid, &value, &bytes);
  return Slice(value, bytes);
}

Status PgApiImpl::RetrieveYbctids(PgStatement *handle, const YBCPgExecParameters *exec_params,
                                      int natts, SliceVector *ybctids, size_t *count,
                                      bool *exceeded_work_mem) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_SELECT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  auto& dml_read = *down_cast<PgDmlRead*>(handle);

  auto vec = new std::vector<Slice>();

  // If there's a secondary index request (secondary index scans or primary key scans on
  // non-colocated tables) return the ybctids from the secondary index request.
  if (dml_read.has_secondary_index_with_doc_op()) {
    RETURN_NOT_OK(dml_read.RetrieveYbctidsFromSecondaryIndex(exec_params, vec,
                                                             exceeded_work_mem));
  } else {
    // Otherwise (colocated case): fetch the results one at a time.
    RETURN_NOT_OK(dml_read.Exec(exec_params));

    uint64_t       *values = new uint64_t[natts];
    bool           *nulls = new bool[natts];
    YBCPgSysColumns syscols;
    bool            has_data;
    size_t          consumed_bytes = 0;

    do {
      RETURN_NOT_OK(dml_read.Fetch(natts, values, nulls, &syscols, &has_data));

      if (has_data && syscols.ybctid != NULL) {
        Slice s = GetYbctidAsSlice((uintptr_t) syscols.ybctid);
        uint8_t *slice_data = new uint8_t[s.size()];
        consumed_bytes += s.size();
        s.relocate(slice_data);
        vec->emplace_back(s);
      }

      if (consumed_bytes >= (size_t) exec_params->work_mem * 1024L) {
        *exceeded_work_mem = true;
        break;
      }
    } while (has_data);

    delete[] values;
    delete[] nulls;
  }

  if (*exceeded_work_mem)
    // delete these allocated ybctids, we won't use them
    for (auto ybctid : *vec)
      delete[] ybctid.cdata(), ybctid.size();

  *count = vec->size();
  *ybctids = vec;
  return Status::OK();
}

Status PgApiImpl::FetchRequestedYbctids(PgStatement *handle, const PgExecParameters *exec_params,
                                        ConstSliceVector ybctids) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_SELECT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  auto& dml_read = *down_cast<PgDmlRead*>(handle);

  RETURN_NOT_OK(dml_read.SetRequestedYbctids((const std::vector<Slice> *) ybctids));

  return dml_read.Exec(exec_params);
}

Status PgApiImpl::DmlANNBindVector(PgStatement *handle, PgExpr *vector) {
  return down_cast<PgDml*>(handle)->ANNBindVector(vector);
}

Status PgApiImpl::DmlANNSetPrefetchSize(PgStatement *handle, int prefetch_size) {
  return down_cast<PgDml*>(handle)->ANNSetPrefetchSize(prefetch_size);
}

Status PgApiImpl::ExecSelect(PgStatement *handle, const PgExecParameters *exec_params) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_SELECT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  auto& dml_read = *down_cast<PgDmlRead*>(handle);
  if (pg_sys_table_prefetcher_ && dml_read.IsReadFromYsqlCatalog() && dml_read.read_req()) {
    // In case of sys tables prefething is enabled all reads from sys table must use cached data.
    auto data = pg_sys_table_prefetcher_->GetData(
        *dml_read.read_req(), dml_read.IsIndexOrderedScan());
    if (!data) {
      // LOG(DFATAL) is used instead of SCHECK to let user on release build proceed by reading
      // data from a master in a non efficient way (by using separate RPC).
      LOG(DFATAL) << "Data was not prefetched for request "
                  << dml_read.read_req()->ShortDebugString();
    } else {
      dml_read.UpgradeDocOp(MakeDocReadOpWithData(pg_session_, std::move(data)));
    }
  }
  return dml_read.Exec(exec_params);
}


//--------------------------------------------------------------------------------------------------
// Functions.
//--------------------------------------------------------------------------------------------------

Status PgApiImpl::NewSRF(
    PgFunction **handle, PgFunctionDataProcessor processor) {
  *handle = nullptr;
  auto func = std::make_unique<PgFunction>(std::move(processor), pg_session_);
  RETURN_NOT_OK(AddToCurrentPgMemctx(std::move(func), handle));
  return Status::OK();
}

Status PgApiImpl::AddFunctionParam(
    PgFunction *handle, const std::string name, const YBCPgTypeEntity *type_entity, uint64_t datum,
    bool is_null) {
  if (!handle) {
    return STATUS(InvalidArgument, "Invalid function handle");
  }

  return handle->AddParam(name, type_entity, datum, is_null);
}

Status PgApiImpl::AddFunctionTarget(
    PgFunction *handle, const std::string name, const YBCPgTypeEntity *type_entity,
    const YBCPgTypeAttrs type_attrs) {
  if (!handle) {
    return STATUS(InvalidArgument, "Invalid function handle");
  }

  return handle->AddTarget(name, type_entity, type_attrs);
}

Status PgApiImpl::FinalizeFunctionTargets(PgFunction *handle) {
  if (!handle) {
    return STATUS(InvalidArgument, "Invalid function handle");
  }

  return handle->FinalizeTargets();
}

Status PgApiImpl::SRFGetNext(PgFunction *handle, uint64_t *values, bool *is_nulls, bool *has_data) {
  if (!handle) {
    return STATUS(InvalidArgument, "Invalid function handle");
  }

  return handle->GetNext(values, is_nulls, has_data);
}

Status PgApiImpl::NewGetLockStatusDataSRF(PgFunction **handle) {
  return NewSRF(handle, PgLockStatusRequestor);
}

//--------------------------------------------------------------------------------------------------
// Expressions.
//--------------------------------------------------------------------------------------------------

// Column references -------------------------------------------------------------------------------

Status PgApiImpl::NewColumnRef(
    PgStatement *stmt, int attr_num, const PgTypeEntity *type_entity, bool collate_is_valid_non_c,
    const PgTypeAttrs *type_attrs, PgExpr **expr_handle) {
  if (!stmt) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  *expr_handle = PgColumnRef::Create(
     &stmt->arena(), attr_num, type_entity, collate_is_valid_non_c, type_attrs);

  return Status::OK();
}

// Constant ----------------------------------------------------------------------------------------
Status PgApiImpl::NewConstant(
    YBCPgStatement stmt, const YBCPgTypeEntity *type_entity, bool collate_is_valid_non_c,
    const char *collation_sortkey, uint64_t datum, bool is_null, YBCPgExpr *expr_handle) {
  if (!stmt) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  *expr_handle = stmt->arena().NewObject<PgConstant>(
      &stmt->arena(), type_entity, collate_is_valid_non_c, collation_sortkey, datum, is_null);

  return Status::OK();
}

Status PgApiImpl::NewConstantVirtual(
    YBCPgStatement stmt, const YBCPgTypeEntity *type_entity,
    YBCPgDatumKind datum_kind, YBCPgExpr *expr_handle) {
  if (!stmt) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  *expr_handle = stmt->arena().NewObject<PgConstant>(
      &stmt->arena(), type_entity, false /* collate_is_valid_non_c */, datum_kind);
  return Status::OK();
}

Status PgApiImpl::NewConstantOp(
    YBCPgStatement stmt, const YBCPgTypeEntity *type_entity, bool collate_is_valid_non_c,
    const char *collation_sortkey, uint64_t datum, bool is_null, YBCPgExpr *expr_handle,
    bool is_gt) {
  if (!stmt) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  *expr_handle = stmt->arena().NewObject<PgConstant>(
      &stmt->arena(), type_entity, collate_is_valid_non_c, collation_sortkey, datum, is_null,
      is_gt ? PgExpr::Opcode::PG_EXPR_GT : PgExpr::Opcode::PG_EXPR_LT);

  return Status::OK();
}

// Text constant -----------------------------------------------------------------------------------

Status PgApiImpl::UpdateConstant(PgExpr *expr, const char *value, bool is_null) {
  if (expr->opcode() != PgExpr::Opcode::PG_EXPR_CONSTANT) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid expression handle for constant");
  }
  down_cast<PgConstant*>(expr)->UpdateConstant(value, is_null);
  return Status::OK();
}

Status PgApiImpl::UpdateConstant(PgExpr *expr, const void *value, int64_t bytes, bool is_null) {
  if (expr->opcode() != PgExpr::Opcode::PG_EXPR_CONSTANT) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid expression handle for constant");
  }
  down_cast<PgConstant*>(expr)->UpdateConstant(value, bytes, is_null);
  return Status::OK();
}

// Text constant -----------------------------------------------------------------------------------

Status PgApiImpl::NewOperator(
    PgStatement *stmt, const char *opname, const YBCPgTypeEntity *type_entity,
    bool collate_is_valid_non_c, PgExpr **op_handle) {
  if (!stmt) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  RETURN_NOT_OK(PgExpr::CheckOperatorName(opname));

  // Create operator.
  *op_handle = PgOperator::Create(&stmt->arena(), opname, type_entity, collate_is_valid_non_c);

  return Status::OK();
}

Status PgApiImpl::OperatorAppendArg(PgExpr *op_handle, PgExpr *arg) {
  if (!op_handle || !arg) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid expression handle");
  }
  down_cast<PgOperator*>(op_handle)->AppendArg(arg);
  return Status::OK();
}

Result<bool> PgApiImpl::IsInitDbDone() {
  return pg_session_->IsInitDbDone();
}

Result<uint64_t> PgApiImpl::GetSharedCatalogVersion(std::optional<PgOid> db_oid) {
  if (!db_oid) {
    return tserver_shared_object_->ysql_catalog_version();
  }
  if (!catalog_version_db_index_) {
    // If db_oid is for a newly created database, it may not have an entry allocated in shared
    // memory. It can also be a race condition case where the database db_oid we are trying to
    // connect to is recently dropped from another node. Let's wait with 500ms interval until the
    // entry shows up or until a 10-second timeout.
    auto status = LoggedWaitFor(
        [this, &db_oid]() -> Result<bool> {
          auto info = VERIFY_RESULT(pg_client_.GetTserverCatalogVersionInfo(
              false /* size_only */, *db_oid));
          // If db_oid does not have an entry allocated in shared memory,
          // info.entries_size() will be 0.
          DCHECK_LE(info.entries_size(), 1) << info.ShortDebugString();
          if (info.entries_size() == 1) {
            RSTATUS_DCHECK_EQ(
                info.entries(0).db_oid(), *db_oid, InternalError,
                Format("Expected database $0, got: $1",
                       *db_oid, info.entries(0).db_oid()));
            catalog_version_db_index_.emplace(*db_oid, info.entries(0).shm_index());
          }
          return catalog_version_db_index_ ? true : false;
        },
        10s /* timeout */,
        Format("Database $0 is not ready in Yugabyte shared memory", *db_oid),
        500ms /* initial_delay */,
        1.0 /* delay_multiplier */);

    RETURN_NOT_OK_PREPEND(
        status,
        Format("Failed to find suitable shared memory index for db $0: $1$2",
               *db_oid, status.ToString(),
               status.IsTimedOut() ? ", there may be too many databases or "
               "the database might have been dropped" : ""));

    CHECK(catalog_version_db_index_);
  }
  if (catalog_version_db_index_->first != *db_oid) {
    return STATUS_FORMAT(
        IllegalState, "Forbidden db switch from $0 to $1 detected",
        catalog_version_db_index_->first, *db_oid);
  }
  return tserver_shared_object_->ysql_db_catalog_version(
      static_cast<size_t>(catalog_version_db_index_->second));
}

Result<uint32_t> PgApiImpl::GetNumberOfDatabases() {
  const auto info = VERIFY_RESULT(pg_client_.GetTserverCatalogVersionInfo(
      true /* size_only */, kPgInvalidOid /* db_oid */));
  return info.num_entries();
}

Result<bool> PgApiImpl::CatalogVersionTableInPerdbMode() {
  DCHECK(FLAGS_ysql_enable_db_catalog_version_mode);
  if (!tserver_shared_object_->catalog_version_table_in_perdb_mode().has_value()) {
    // If this tserver has just restarted, it may not have received any
    // heartbeat response from yb-master that has set a value in
    // catalog_version_table_in_perdb_mode_ in the shared memory object
    // yet. Let's wait with 500ms interval until a value is set or until
    // a 10-second timeout.
    auto status = LoggedWaitFor(
        [this]() -> Result<bool> {
          return tserver_shared_object_->catalog_version_table_in_perdb_mode().has_value();
        },
        10s /* timeout */,
        "catalog_version_table_in_perdb_mode is not set in shared memory",
        500ms /* initial_delay */,
        1.0 /* delay_multiplier */);
    RETURN_NOT_OK_PREPEND(
        status,
        "Failed to find out pg_yb_catalog_version mode");
  }
  return tserver_shared_object_->catalog_version_table_in_perdb_mode().value();
}

uint64_t PgApiImpl::GetSharedAuthKey() const {
  return tserver_shared_object_->postgres_auth_key();
}

const unsigned char *PgApiImpl::GetLocalTserverUuid() const {
  return tserver_shared_object_->tserver_uuid();
}

pid_t PgApiImpl::GetLocalTServerPid() const {
  return tserver_shared_object_->pid();
}

// Tuple Expression -----------------------------------------------------------------------------
Status PgApiImpl::NewTupleExpr(
    YBCPgStatement stmt, const YBCPgTypeEntity *tuple_type_entity,
    const YBCPgTypeAttrs *type_attrs, int num_elems,
    const YBCPgExpr *elems, YBCPgExpr *expr_handle) {
  if (!stmt) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  *expr_handle = stmt->arena().NewObject<PgTupleExpr>(
      &stmt->arena(), tuple_type_entity, type_attrs, num_elems, elems);

  return Status::OK();
}

// Transaction Control -----------------------------------------------------------------------------
Status PgApiImpl::BeginTransaction(int64_t start_time) {
  pg_session_->InvalidateForeignKeyReferenceCache();
  return pg_txn_manager_->BeginTransaction(start_time);
}

Status PgApiImpl::RecreateTransaction() {
  ClearSessionState();
  return pg_txn_manager_->RecreateTransaction();
}

Status PgApiImpl::RestartTransaction() {
  ClearSessionState();
  return pg_txn_manager_->RestartTransaction();
}

Status PgApiImpl::ResetTransactionReadPoint() {
  return pg_txn_manager_->ResetTransactionReadPoint();
}

Status PgApiImpl::EnsureReadPoint() {
  return pg_txn_manager_->EnsureReadPoint();
}

Status PgApiImpl::RestartReadPoint() {
  return pg_txn_manager_->RestartReadPoint();
}

bool PgApiImpl::IsRestartReadPointRequested() {
  return pg_txn_manager_->IsRestartReadPointRequested();
}

Status PgApiImpl::CommitPlainTransaction() {
  DCHECK(pg_session_->explicit_row_lock_buffer().IsEmpty());
  pg_session_->InvalidateForeignKeyReferenceCache();
  RETURN_NOT_OK(pg_session_->FlushBufferedOperations());
  return pg_txn_manager_->CommitPlainTransaction();
}

Status PgApiImpl::AbortPlainTransaction() {
  ClearSessionState();
  return pg_txn_manager_->AbortPlainTransaction();
}

Status PgApiImpl::SetTransactionIsolationLevel(int isolation) {
  return pg_txn_manager_->SetPgIsolationLevel(isolation);
}

Status PgApiImpl::SetTransactionReadOnly(bool read_only) {
  return pg_txn_manager_->SetReadOnly(read_only);
}

Status PgApiImpl::SetEnableTracing(bool tracing) {
  return pg_txn_manager_->SetEnableTracing(tracing);
}

Status PgApiImpl::UpdateFollowerReadsConfig(bool enable_follower_reads, int32_t staleness_ms) {
  return pg_txn_manager_->UpdateFollowerReadsConfig(enable_follower_reads, staleness_ms);
}

Status PgApiImpl::SetTransactionDeferrable(bool deferrable) {
  return pg_txn_manager_->SetDeferrable(deferrable);
}

Status PgApiImpl::SetInTxnBlock(bool in_txn_blk) {
  return pg_txn_manager_->SetInTxnBlock(in_txn_blk);
}

Status PgApiImpl::SetReadOnlyStmt(bool read_only_stmt) {
  return pg_txn_manager_->SetReadOnlyStmt(read_only_stmt);
}

Status PgApiImpl::EnterSeparateDdlTxnMode() {
  // Flush all buffered operations as ddl txn use its own transaction session.
  RETURN_NOT_OK(pg_session_->FlushBufferedOperations());
  pg_session_->ResetHasWriteOperationsInDdlMode();
  return pg_txn_manager_->EnterSeparateDdlTxnMode();
}

bool PgApiImpl::HasWriteOperationsInDdlTxnMode() const {
  return pg_session_->HasWriteOperationsInDdlMode();
}

Status PgApiImpl::ExitSeparateDdlTxnMode(PgOid db_oid, bool is_silent_modification) {
  // Flush all buffered operations as ddl txn use its own transaction session.
  RETURN_NOT_OK(pg_session_->FlushBufferedOperations());
  RETURN_NOT_OK(pg_txn_manager_->ExitSeparateDdlTxnModeWithCommit(db_oid, is_silent_modification));
  // Next reads from catalog tables have to see changes made by the DDL transaction.
  ResetCatalogReadTime();
  return Status::OK();
}

Status PgApiImpl::ClearSeparateDdlTxnMode() {
  ClearSessionState();
  return pg_txn_manager_->ExitSeparateDdlTxnModeWithAbort();
}

Status PgApiImpl::SetActiveSubTransaction(SubTransactionId id) {
  RETURN_NOT_OK(pg_session_->FlushBufferedOperations());
  return pg_session_->SetActiveSubTransaction(id);
}

Status PgApiImpl::RollbackToSubTransaction(SubTransactionId id) {
  pg_session_->DropBufferedOperations();
  pg_session_->explicit_row_lock_buffer().Clear();
  return pg_session_->RollbackToSubTransaction(id);
}

double PgApiImpl::GetTransactionPriority() const {
  return pg_txn_manager_->GetTransactionPriority();
}

TxnPriorityRequirement PgApiImpl::GetTransactionPriorityType() const {
  return pg_txn_manager_->GetTransactionPriorityType();
}

Result<Uuid> PgApiImpl::GetActiveTransaction() const {
  Uuid result;
  RETURN_NOT_OK(pg_client_.EnumerateActiveTransactions(
      make_lw_function(
          [&result](
              const tserver::PgGetActiveTransactionListResponsePB_EntryPB& entry, bool is_last) {
            DCHECK(is_last);
            result = VERIFY_RESULT(Uuid::FromSlice(Slice(entry.txn_id())));
            return static_cast<Status>(Status::OK());
          }),
      /*for_current_session_only=*/true));

  return result;
}

Status PgApiImpl::GetActiveTransactions(YBCPgSessionTxnInfo* infos, size_t num_infos) {
  std::unordered_map<uint64_t, Slice> txns;
  txns.reserve(num_infos);
  return pg_client_.EnumerateActiveTransactions(make_lw_function(
      [&txns, infos, num_infos](
          const tserver::PgGetActiveTransactionListResponsePB_EntryPB& entry, bool is_last) {
        txns.emplace(entry.session_id(), entry.txn_id());
        if (is_last) {
          for (auto* i = infos, *end = i + num_infos; i != end; ++i) {
            auto txn = txns.find(i->session_id);
            if (txn != txns.end()) {
              auto uuid = VERIFY_RESULT(Uuid::FromSlice(txn->second));
              uuid.ToBytes(i->txn_id.data);
              i->is_not_null = true;
            }
          }
        }
        return static_cast<Status>(Status::OK());
      }));
}

bool PgApiImpl::IsDdlMode() const {
  return pg_txn_manager_->IsDdlMode();
}

void PgApiImpl::ResetCatalogReadTime() {
  pg_session_->ResetCatalogReadPoint();
}

Result<bool> PgApiImpl::ForeignKeyReferenceExists(
    PgOid table_id, const Slice& ybctid, PgOid database_id) {
  return pg_session_->ForeignKeyReferenceExists(
      LightweightTableYbctid(table_id, ybctid),
      make_lw_function(
        [this, database_id](TableYbctidVector* ybctids,
                            const OidSet& region_local_tables) {
          return FetchExistingYbctids(
              pg_session_, database_id, ybctids, region_local_tables,
              [](PgExecParameters* exec_params) {exec_params->rowmark = ROW_MARK_KEYSHARE;});
        }));
}

void PgApiImpl::AddForeignKeyReferenceIntent(
    PgOid table_id, bool is_region_local, const Slice& ybctid) {
  pg_session_->AddForeignKeyReferenceIntent(
      LightweightTableYbctid(table_id, ybctid), is_region_local);
}

void PgApiImpl::DeleteForeignKeyReference(PgOid table_id, const Slice& ybctid) {
  pg_session_->DeleteForeignKeyReference(LightweightTableYbctid(table_id, ybctid));
}

void PgApiImpl::AddForeignKeyReference(PgOid table_id, const Slice& ybctid) {
  pg_session_->AddForeignKeyReference(LightweightTableYbctid(table_id, ybctid));
}

Status PgApiImpl::AddExplicitRowLockIntent(
    const PgObjectId& table_id, const Slice& ybctid,
    const PgExplicitRowLockParams& params, bool is_region_local) {
  return pg_session_->explicit_row_lock_buffer().Add(
      {.rowmark = params.rowmark,
       .pg_wait_policy = params.pg_wait_policy,
       .docdb_wait_policy = params.docdb_wait_policy,
       .database_id = table_id.database_oid},
      LightweightTableYbctid(table_id.object_oid, ybctid),
      is_region_local,
      make_lw_function(MakeYbctidReaderForExplicitRowLock(pg_session_)));
}

Status PgApiImpl::FlushExplicitRowLockIntents() {
  return pg_session_->explicit_row_lock_buffer().Flush(
      make_lw_function(MakeYbctidReaderForExplicitRowLock(pg_session_)));
}

void PgApiImpl::SetTimeout(int timeout_ms) {
  pg_session_->SetTimeout(timeout_ms);
}

Result<yb::tserver::PgGetLockStatusResponsePB> PgApiImpl::GetLockStatusData(
    const std::string &table_id, const std::string &transaction_id) {
  return pg_session_->GetLockStatusData(table_id, transaction_id);
}

Result<client::TabletServersInfo> PgApiImpl::ListTabletServers() {
  return pg_session_->ListTabletServers();
}

Status PgApiImpl::GetIndexBackfillProgress(std::vector<PgObjectId> oids,
                                           uint64_t** backfill_statuses) {
  return pg_session_->GetIndexBackfillProgress(oids, backfill_statuses);
}

Status PgApiImpl::ValidatePlacement(const char *placement_info) {
  return pg_session_->ValidatePlacement(placement_info);
}

void PgApiImpl::StartSysTablePrefetching(const PrefetcherOptions& options) {
  if (pg_sys_table_prefetcher_) {
    LOG(DFATAL) << "Sys table prefetching was started already";
  }

  ResetCatalogReadTime();
  pg_sys_table_prefetcher_.emplace(options);
}

void PgApiImpl::StopSysTablePrefetching() {
  if (!pg_sys_table_prefetcher_) {
    LOG(DFATAL) << "Sys table prefetching was not started yet";
  } else {
    pg_sys_table_prefetcher_.reset();
    ResetCatalogReadTime();
  }
}

bool PgApiImpl::IsSysTablePrefetchingStarted() const {
  return static_cast<bool>(pg_sys_table_prefetcher_);
}

Status PgApiImpl::PrefetchRegisteredSysTables() {
  SCHECK(pg_sys_table_prefetcher_, IllegalState, "Sys table prefetching has not been started");
  return pg_sys_table_prefetcher_->Prefetch(pg_session_.get());
}

void PgApiImpl::RegisterSysTableForPrefetching(
  const PgObjectId& table_id, const PgObjectId& index_id, int row_oid_filtering_attr) {
  if (!pg_sys_table_prefetcher_) {
    LOG(DFATAL) << "Sys table prefetching was not started yet";
  } else {
    pg_sys_table_prefetcher_->Register(table_id, index_id, row_oid_filtering_attr);
  }
}

Result<bool> PgApiImpl::CheckIfPitrActive() {
  return pg_session_->CheckIfPitrActive();
}

Result<bool> PgApiImpl::IsObjectPartOfXRepl(const PgObjectId& table_id) {
  return pg_session_->IsObjectPartOfXRepl(table_id);
}

Result<TableKeyRanges> PgApiImpl::GetTableKeyRanges(
    const PgObjectId& table_id, Slice lower_bound_key, Slice upper_bound_key,
    uint64_t max_num_ranges, uint64_t range_size_bytes, bool is_forward, uint32_t max_key_length) {
  return pg_session_->GetTableKeyRanges(
      table_id, lower_bound_key, upper_bound_key, max_num_ranges, range_size_bytes, is_forward,
      max_key_length);
}

void PgApiImpl::DumpSessionState(YBCPgSessionState* session_data) {
  session_data->session_id = GetSessionID();
  pg_txn_manager_->DumpSessionState(session_data);
}

void PgApiImpl::RestoreSessionState(const YBCPgSessionState& session_data) {
  DCHECK_EQ(GetSessionID(), session_data.session_id);
  pg_txn_manager_->RestoreSessionState(session_data);
}

//--------------------------------------------------------------------------------------------------

Status PgApiImpl::NewCreateReplicationSlot(const char *slot_name,
                                           const char *plugin_name,
                                           const PgOid database_oid,
                                           YBCPgReplicationSlotSnapshotAction snapshot_action,
                                           PgStatement **handle) {
  auto stmt = std::make_unique<PgCreateReplicationSlot>(
      pg_session_, slot_name, plugin_name, database_oid, snapshot_action);
  RETURN_NOT_OK(AddToCurrentPgMemctx(std::move(stmt), handle));
  return Status::OK();
}

Result<tserver::PgCreateReplicationSlotResponsePB> PgApiImpl::ExecCreateReplicationSlot(
    PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_CREATE_REPLICATION_SLOT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  PgCreateReplicationSlot *pg_stmt = down_cast<PgCreateReplicationSlot*>(handle);
  return pg_stmt->Exec();
}

Result<tserver::PgListReplicationSlotsResponsePB> PgApiImpl::ListReplicationSlots() {
  return pg_session_->ListReplicationSlots();
}

Result<tserver::PgGetReplicationSlotResponsePB> PgApiImpl::GetReplicationSlot(
    const ReplicationSlotName& slot_name) {
  return pg_session_->GetReplicationSlot(slot_name);
}

Result<cdc::InitVirtualWALForCDCResponsePB> PgApiImpl::InitVirtualWALForCDC(
    const std::string& stream_id, const std::vector<PgObjectId>& table_ids) {
  return pg_session_->pg_client().InitVirtualWALForCDC(stream_id, table_ids);
}

Result<cdc::UpdatePublicationTableListResponsePB> PgApiImpl::UpdatePublicationTableList(
    const std::string& stream_id, const std::vector<PgObjectId>& table_ids) {
  return pg_session_->pg_client().UpdatePublicationTableList(stream_id, table_ids);
}

Result<cdc::DestroyVirtualWALForCDCResponsePB> PgApiImpl::DestroyVirtualWALForCDC() {
  return pg_session_->pg_client().DestroyVirtualWALForCDC();
}

Result<cdc::GetConsistentChangesResponsePB> PgApiImpl::GetConsistentChangesForCDC(
    const std::string &stream_id) {
  return pg_session_->pg_client().GetConsistentChangesForCDC(stream_id);
}

Result<cdc::UpdateAndPersistLSNResponsePB> PgApiImpl::UpdateAndPersistLSN(
    const std::string& stream_id, YBCPgXLogRecPtr restart_lsn, YBCPgXLogRecPtr confirmed_flush) {
  return pg_session_->pg_client().UpdateAndPersistLSN(stream_id, restart_lsn, confirmed_flush);
}

Status PgApiImpl::NewDropReplicationSlot(const char *slot_name,
                                         PgStatement **handle) {
  auto stmt = std::make_unique<PgDropReplicationSlot>(pg_session_, slot_name);
  RETURN_NOT_OK(AddToCurrentPgMemctx(std::move(stmt), handle));
  return Status::OK();
}

Status PgApiImpl::ExecDropReplicationSlot(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_DROP_REPLICATION_SLOT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  PgDropReplicationSlot *pg_stmt = down_cast<PgDropReplicationSlot*>(handle);
  return pg_stmt->Exec();
}

Result<tserver::PgYCQLStatementStatsResponsePB> PgApiImpl::YCQLStatementStats() {
  return pg_session_->YCQLStatementStats();
}

Result<tserver::PgActiveSessionHistoryResponsePB> PgApiImpl::ActiveSessionHistory() {
  return pg_session_->ActiveSessionHistory();
}

Result<tserver::PgTabletsMetadataResponsePB> PgApiImpl::TabletsMetadata() {
  return pg_session_->TabletsMetadata();
}

void PgApiImpl::ClearSessionState() {
  pg_session_->InvalidateForeignKeyReferenceCache();
  pg_session_->DropBufferedOperations();
  pg_session_->explicit_row_lock_buffer().Clear();
}

bool PgApiImpl::IsCronLeader() const { return tserver_shared_object_->IsCronLeader(); }

uint64_t PgApiImpl::GetCurrentReadTimePoint() const {
  return pg_txn_manager_->GetCurrentReadTimePoint();
}

Status PgApiImpl::RestoreReadTimePoint(uint64_t read_time_point_handle) {
  RETURN_NOT_OK(FlushBufferedOperations());
  return pg_txn_manager_->RestoreReadTimePoint(read_time_point_handle);
}

} // namespace yb::pggate
