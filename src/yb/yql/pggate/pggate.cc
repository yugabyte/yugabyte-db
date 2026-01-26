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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pggate.h"

#include <algorithm>
#include <array>
#include <concepts>
#include <initializer_list>
#include <iterator>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include <ev++.h>

#include "yb/ash/pg_wait_state.h"
#include "yb/ash/rpc_wait_state.h"

#include "yb/client/client_utils.h"
#include "yb/client/table_info.h"

#include "yb/dockv/partition.h"
#include "yb/common/pg_system_attr.h"
#include "yb/common/schema.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/value_type.h"

#include "yb/gutil/casts.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"
#include "yb/rpc/secure_stream.h"

#include "yb/rpc/secure.h"

#include "yb/tserver/pg_client.pb.h"
#include "yb/tserver/tserver_shared_mem.h"

#include "yb/util/alignment.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/enums.h"
#include "yb/util/format.h"
#include "yb/util/metrics.h"
#include "yb/util/range.h"
#include "yb/util/status_format.h"
#include "yb/util/thread.h"
#include "yb/util/scope_exit.h"

#include "yb/yql/pggate/pg_column.h"
#include "yb/yql/pggate/pg_ddl.h"
#include "yb/yql/pggate/pg_delete.h"
#include "yb/yql/pggate/pg_dml.h"
#include "yb/yql/pggate/pg_dml_read.h"
#include "yb/yql/pggate/pg_dml_write.h"
#include "yb/yql/pggate/pg_explicit_row_lock_buffer.h"
#include "yb/yql/pggate/pg_flush_debug_context.h"
#include "yb/yql/pggate/pg_function.h"
#include "yb/yql/pggate/pg_insert.h"
#include "yb/yql/pggate/pg_memctx.h"
#include "yb/yql/pggate/pg_sample.h"
#include "yb/yql/pggate/pg_select.h"
#include "yb/yql/pggate/pg_select_index.h"
#include "yb/yql/pggate/pg_session.h"
#include "yb/yql/pggate/pg_shared_mem.h"
#include "yb/yql/pggate/pg_statement.h"
#include "yb/yql/pggate/pg_tabledesc.h"
#include "yb/yql/pggate/pg_tools.h"
#include "yb/yql/pggate/pg_truncate_colocated.h"
#include "yb/yql/pggate/pg_txn_manager.h"
#include "yb/yql/pggate/pg_update.h"
#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"
#include "yb/yql/pggate/ybc_pggate.h"

using namespace std::literals;

DECLARE_bool(use_node_to_node_encryption);
DECLARE_string(certs_dir);
DECLARE_bool(node_to_node_encryption_use_client_certificates);
DECLARE_int32(backfill_index_client_rpc_timeout_ms);
DECLARE_uint32(wait_for_ysql_backends_catalog_version_client_master_rpc_margin_ms);
DECLARE_uint32(wait_for_ysql_backends_catalog_version_client_master_rpc_timeout_ms);

DEFINE_RUNTIME_PREVIEW_bool(ysql_pack_inserted_value, false,
     "Enabled packing inserted columns into a single packed value in postgres layer.");

DECLARE_bool(enable_object_locking_for_table_locks);

namespace yb::pggate {
namespace {

Status AddColumn(
    PgCreateTableBase& create_table, const char* attr_name, int attr_num,
    const YbcPgTypeEntity* attr_type, bool is_hash, bool is_range, bool is_desc,
    bool is_nulls_first) {
  auto sorting_type = SortingType::kNotSpecified;

  if (!is_hash && is_range) {
    if (is_desc) {
      sorting_type = is_nulls_first ? SortingType::kDescending : SortingType::kDescendingNullsLast;
    } else {
      sorting_type = is_nulls_first ? SortingType::kAscending : SortingType::kAscendingNullsLast;
    }
  }

  return create_table.AddColumn(attr_name, attr_num, attr_type, is_hash, is_range, sorting_type);
}

Result<PgApiImpl::MessengerHolder> BuildMessenger(
    const std::string& client_name, int32_t num_reactors,
    const scoped_refptr<MetricEntity>& metric_entity,
    const std::shared_ptr<MemTracker>& parent_mem_tracker) {
  std::unique_ptr<rpc::SecureContext> secure_context;
  if (FLAGS_use_node_to_node_encryption) {
    secure_context = VERIFY_RESULT(rpc::CreateSecureContext(
        FLAGS_certs_dir,
        rpc::UseClientCerts(FLAGS_node_to_node_encryption_use_client_certificates),
        FLAGS_pggate_cert_base_name));
  }
  return PgApiImpl::MessengerHolder{
      std::move(secure_context),
      VERIFY_RESULT(client::CreateClientMessenger(
          client_name, num_reactors, metric_entity, parent_mem_tracker, secure_context.get()))};
}

class ExplicitRowLockErrorInfoAdapter {
 public:
  explicit ExplicitRowLockErrorInfoAdapter(YbcPgExplicitRowLockErrorInfo& pg_error_info)
    : pg_error_info_(pg_error_info) {}

  ~ExplicitRowLockErrorInfoAdapter() {
    if (error_info_) {
      pg_error_info_.is_initialized = true;
      pg_error_info_.pg_wait_policy = error_info_->pg_wait_policy;
      pg_error_info_.conflicting_table_id = error_info_->conflicting_table_id;
    }
  }

  operator std::optional<ExplicitRowLockBuffer::ErrorStatusAdditionalInfo>&() {
    return error_info_;
  }

 private:
  YbcPgExplicitRowLockErrorInfo& pg_error_info_;
  std::optional<ExplicitRowLockBuffer::ErrorStatusAdditionalInfo> error_info_;
};

std::optional<PgSelect::IndexQueryInfo> MakeIndexQueryInfo(
    const PgObjectId& index_id, const YbcPgPrepareParameters* params) {
  if (!index_id.IsValid()) {
    return std::nullopt;
  }
  return PgSelect::IndexQueryInfo{index_id, params && params->embedded_idx};
}

Result<std::unique_ptr<PgStatement>> MakeSelectStatement(
    const PgSession::ScopedRefPtr& pg_session, const PgObjectId& table_id,
    const PgObjectId& index_id, const YbcPgPrepareParameters* params,
    const YbcPgTableLocalityInfo& locality_info) {
  if (params && params->index_only_scan) {
    return PgSelectIndex::Make(pg_session, index_id, locality_info);
  }
  return PgSelect::Make(
      pg_session, table_id, locality_info, MakeIndexQueryInfo(index_id, params));
}

namespace get_statement_as::internal {

template <class T>
concept SingleStmtOpSubclass =
    std::derived_from<T, PgStatementLeafBase<typename T::BaseParam, T::kStmtOp>>;

template <class T>
concept GroupStmtOpSubclass = PgStatementSubclass<T> && !SingleStmtOpSubclass<T>;

template <PgStatementSubclass T>
struct StatementTag {
  constexpr StatementTag() : stmt_op(T::kStmtOp) {}

  template<PgStatementSubclass U>
  requires(GroupStmtOpSubclass<T> && std::derived_from<U, T>)
  constexpr StatementTag(const StatementTag<U>& t) : stmt_op(t.stmt_op) {}

  const StmtOp stmt_op;
};

template <GroupStmtOpSubclass T>
using StatementTags = std::initializer_list<StatementTag<T>>;

template <GroupStmtOpSubclass T>
struct TagsResolver;

template <>
struct TagsResolver<PgCreateTableBase> {
  static constexpr StatementTags<PgCreateTableBase> kTags = {
      StatementTag<PgCreateTable>(),
      StatementTag<PgCreateIndex>()
  };
};

template <>
struct TagsResolver<PgDdl> {
  static constexpr StatementTags<PgDdl> kTags = {
      // PgCreateTableBase
      StatementTag<PgCreateTable>(),
      StatementTag<PgCreateIndex>(),

      StatementTag<PgAlterDatabase>(),
      StatementTag<PgCreateDatabase>(),
      StatementTag<PgDropDatabase>(),
      StatementTag<PgCreateTablegroup>(),
      StatementTag<PgDropTablegroup>(),
      StatementTag<PgAlterTable>(),
      StatementTag<PgDropTable>(),
      StatementTag<PgDropIndex>(),
      StatementTag<PgDropSequence>(),
      StatementTag<PgDropDBSequences>(),
      StatementTag<PgCreateReplicationSlot>(),
      StatementTag<PgDropReplicationSlot>(),
      StatementTag<PgTruncateTable>()
  };
};

template <>
struct TagsResolver<PgDmlRead> {
  static constexpr StatementTags<PgDmlRead> kTags = {
      StatementTag<PgSample>(),
      StatementTag<PgSelect>()
  };
};

template <>
struct TagsResolver<PgDmlWrite> {
  static constexpr StatementTags<PgDmlWrite> kTags = {
      StatementTag<PgInsert>(),
      StatementTag<PgDelete>(),
      StatementTag<PgTruncateColocated>(),
      StatementTag<PgUpdate>()
  };
};

template <>
struct TagsResolver<PgDml> {
  static constexpr StatementTags<PgDml> kTags = {
      // PgDmlRead
      StatementTag<PgSample>(),
      StatementTag<PgSelect>(),

      // PgDmlWrite
      StatementTag<PgInsert>(),
      StatementTag<PgDelete>(),
      StatementTag<PgTruncateColocated>(),
      StatementTag<PgUpdate>()
  };
};

template <std::forward_iterator Iterator, GroupStmtOpSubclass T, class... Args>
constexpr bool AreTagsCoveringSubtags(
    Iterator i, Iterator end, StatementTags<T> subtags, Args&&... args) {
  if constexpr (!sizeof...(Args)) {
    for (auto st : subtags) {
      if (i == end || (*i++).stmt_op != st.stmt_op) {
        return false;
      }
    }
    return true;
  } else {
    return AreTagsCoveringSubtags(i, end, subtags) &&
           AreTagsCoveringSubtags(i + subtags.size(), end, std::forward<Args>(args)...);
  }
}

template <GroupStmtOpSubclass T, class... Args>
constexpr bool AreTagsCoveringSubtags(StatementTags<T> tags, Args&&... args) {
  return AreTagsCoveringSubtags(tags.begin(), tags.end(), std::forward<Args>(args)...);
}

using AllStmtOpsBoolArray = std::array<bool, MapSize(static_cast<StmtOp*>(nullptr))>;

template <GroupStmtOpSubclass T, class... Args>
constexpr std::pair<size_t, bool> UniqTagsCount(
    AllStmtOpsBoolArray& found_items, StatementTags<T> tags, Args&&... args) {
  size_t count = 0;
  bool has_duplicates = false;
  if constexpr (sizeof...(Args)) {
    std::tie(count, has_duplicates) = UniqTagsCount(found_items, std::forward<Args>(args)...);
  }
  for (auto t : tags) {
    const auto idx = std::to_underlying(t.stmt_op);
    if (idx < found_items.size() && !found_items.data()[idx]) {
      ++count;
      found_items.data()[idx] = true;
    } else {
      has_duplicates = true;
    }
  }
  return {count, has_duplicates};
}

template <class... Args>
constexpr bool DoesTagsSpanAllStmtOps(Args&&... args) {
  AllStmtOpsBoolArray found_items = {};
  size_t uniq_count = 0;
  bool has_duplicates = false;
  std::tie(uniq_count, has_duplicates) = UniqTagsCount(found_items, std::forward<Args>(args)...);
  return !has_duplicates && uniq_count == NumEnumElements(static_cast<StmtOp*>(nullptr));
}

static_assert(
    AreTagsCoveringSubtags(
        TagsResolver<PgDml>::kTags,
        TagsResolver<PgDmlRead>::kTags, TagsResolver<PgDmlWrite>::kTags) &&
    AreTagsCoveringSubtags(TagsResolver<PgDdl>::kTags, TagsResolver<PgCreateTableBase>::kTags) &&
    DoesTagsSpanAllStmtOps(TagsResolver<PgDml>::kTags, TagsResolver<PgDdl>::kTags));

template <SingleStmtOpSubclass T>
Result<T&> GetStatementAs(PgStatement* handle) {
  RSTATUS_DCHECK(
      handle && handle->stmt_op() == T::kStmtOp, InvalidArgument, "Invalid statement handle");
  return down_cast<T&>(*handle);
}

template <GroupStmtOpSubclass T>
bool IsStmtOpAllowed(StmtOp stmt_op, StatementTags<T> tags) {
  for (auto t : tags) {
    if (t.stmt_op == stmt_op) {
      return true;
    }
  }
  return false;
}

template <GroupStmtOpSubclass T>
Result<T&> GetStatementAs(PgStatement* handle, StatementTags<T> tags) {
  RSTATUS_DCHECK(
      handle && IsStmtOpAllowed(handle->stmt_op(), tags),
      InvalidArgument, "Invalid statement handle");

  return down_cast<T&>(*handle);
}

template <GroupStmtOpSubclass T>
auto GetStatementAs(PgStatement* handle) {
  return GetStatementAs<T>(handle, TagsResolver<T>::kTags);
}

} // namespace get_statement_as::internal

using get_statement_as::internal::GetStatementAs;
using get_statement_as::internal::StatementTag;

Result<ThreadSafeArena&> GetArena(PgStatement* handle) {
  RSTATUS_DCHECK(handle, InvalidArgument, "Invalid statement handle");
  return handle->arena();
}

template <class T>
requires(std::derived_from<T, PgDdl>)
Status ExecDdlWithSyscatalogChanges(PgStatement* handle, PgSession& session) {
  auto& stmt = VERIFY_RESULT_REF(GetStatementAs<T>(handle));
  session.SetDdlHasSyscatalogChanges();
  return stmt.Exec();
}

Result<bool> RetrieveYbctidsFromIndex(
    PgSelectIndex& index, std::vector<Slice>& ybctids, size_t max_mem_bytes) {
  size_t consumed_bytes = 0;

  for (;;) {
    auto batch = VERIFY_RESULT(index.FetchYbctidBatch());
    if (!batch) {
      break;
    }
    ybctids.reserve(ybctids.size() + batch->ybctids.size());
    for(auto ybctid : batch->ybctids) {
      const auto sz = ybctid.size();
      if (consumed_bytes += sz > max_mem_bytes) {
        return false;
      }
      ybctid.relocate(new uint8_t[sz]);
      ybctids.push_back(ybctid);
    }
  }
  return true;
}

Result<bool> RetrieveYbctidsImpl(
    const PgTypeInfo& pg_types, PgDmlRead& dml_read, int natts, size_t max_mem_bytes,
    std::vector<Slice>& ybctids) {
  if (dml_read.IsPgSelectIndex()) {
    return RetrieveYbctidsFromIndex(down_cast<PgSelectIndex&>(dml_read), ybctids, max_mem_bytes);
  }
  std::unique_ptr<uint64_t[]> values{new uint64_t[natts]};
  std::unique_ptr<bool[]> nulls{new bool[natts]};
  YbcPgSysColumns syscols;
  size_t consumed_bytes = 0;
  for(bool has_data = true;;) {
    RETURN_NOT_OK(dml_read.Fetch(natts, values.get(), nulls.get(), &syscols, &has_data));
    if (!has_data) {
      break;
    }
    if (syscols.ybctid) {
      auto s = YbctidAsSlice(pg_types, reinterpret_cast<uint64_t>(syscols.ybctid));
      const auto sz = s.size();
      if (consumed_bytes += sz > max_mem_bytes) {
        return false;
      }
      s.relocate(new uint8_t[s.size()]);
      ybctids.push_back(s);
    }
  }
  return true;
}

[[nodiscard]] auto UpdateCatalogReadTime(PgSession& session, const ReadHybridTime& read_time) {
  DCHECK(read_time);
  const auto current_catalog_read_time = session.catalog_read_time();
  session.TrySetCatalogReadPoint(read_time);
  return MakeOptionalScopeExit(
      [&session, current_catalog_read_time] {
        session.TrySetCatalogReadPoint(current_catalog_read_time);
      });
}

[[nodiscard]] bool ShouldEnableTableLocks() {
  return FLAGS_enable_object_locking_for_table_locks && !YBCIsInitDbModeEnvVarSet() &&
         !YBCIsBinaryUpgrade();
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

PgApiImpl::MessengerHolder::MessengerHolder(
    std::unique_ptr<rpc::SecureContext>&& security_context_,
    std::unique_ptr<rpc::Messenger>&& messenger_)
    : security_context(std::move(security_context_)), messenger(std::move(messenger_)) {}

PgApiImpl::MessengerHolder::MessengerHolder(MessengerHolder&&) = default;
PgApiImpl::MessengerHolder::~MessengerHolder() = default;

//--------------------------------------------------------------------------------------------------

// Helper class to shutdown RPC messenger in async-signal-safe manner.
// On interrupt request class resumes separate thread is async-signal-safe manner to perform
// non-async-signal-safe messenger shutdown.
class PgApiImpl::Interrupter {
 public:
  Interrupter(rpc::Messenger& messenger, PgClient& pg_client)
      : messenger_(messenger), pg_client_(pg_client) {
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
  void AsyncHandler(ev::async& async, int events) {
    messenger_.Shutdown();
    pg_client_.Interrupt();
    loop_.break_loop();
  }

  void RunThread() {
    loop_.run();
  }

  rpc::Messenger& messenger_;
  PgClient& pg_client_;
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
    PgSession* session, const YbcPgYBTupleIdDescriptor& descr) {
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
      values->emplace_back(dockv::KeyEntryValue::NullValue(column.desc().sorting_type()));
      continue;
    }
    if (attr->attr_num == std::to_underlying(PgSystemAttrNum::kYBRowId)) {
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

struct PgApiImpl::PgSharedData {
  std::atomic<uint64_t> next_perform_op_serial_no{0};
};

static_assert(std::is_trivially_destructible_v<PgApiImpl::PgSharedData>);

struct PgApiImpl::SignedPgSharedData {
  std::atomic<uint64_t> signature{kMagicSignature};
  PgSharedData data;

  ~SignedPgSharedData() {
    signature.store(0, std::memory_order_release);
  }

  constexpr static uint64_t kMagicSignature = 0x0112031404130211;
};

static_assert(
    sizeof(PgApiImpl::SignedPgSharedData) + alignof(PgApiImpl::SignedPgSharedData)
        <= sizeof(YbcPgSharedDataPlaceholder));

PgApiImpl::PgSharedDataHolder::PgSharedDataHolder(
    YbcPgSharedDataPlaceholder& raw_data, bool is_owner)
    : is_owner_(is_owner) {
  auto* placeholder = pointer_cast<char*>(&raw_data);
  auto* aligned_placeholder = align_up(placeholder, alignof(SignedPgSharedData));
  // Paranoid check
  DCHECK_LE(
      aligned_placeholder + sizeof(SignedPgSharedData),
      placeholder + sizeof(YbcPgSharedDataPlaceholder));
  if (is_owner_) {
    signed_data_ = new (aligned_placeholder) SignedPgSharedData{};
    CHECK(signed_data_->data.next_perform_op_serial_no.is_lock_free());
  } else {
    signed_data_ = pointer_cast<SignedPgSharedData*>(aligned_placeholder);
    CHECK_EQ(
        signed_data_->signature.load(std::memory_order_acquire),
        SignedPgSharedData::kMagicSignature);
  }
}

PgApiImpl::PgSharedDataHolder::~PgSharedDataHolder() {
  if (is_owner_) {
    signed_data_->~SignedPgSharedData();
  }
}

PgApiImpl::PgSharedData* PgApiImpl::PgSharedDataHolder::operator->() {
  return &signed_data_->data;
}

//--------------------------------------------------------------------------------------------------

PgApiImpl::PgApiImpl(
    YbcPgTypeEntities type_entities, const YbcPgCallbacks& callbacks,
    const YbcPgInitPostgresInfo& init_postgres_info, YbcPgAshConfig& ash_config,
    YbcPgExecStatsState& session_stats, bool is_binary_upgrade)
    : pg_types_(type_entities),
      metric_registry_(new MetricRegistry()),
      metric_entity_(METRIC_ENTITY_server.Instantiate(metric_registry_.get(), "yb.pggate")),
      mem_tracker_(MemTracker::CreateTracker("PostgreSQL")),
      messenger_holder_(CHECK_RESULT(BuildMessenger(
          "pggate_ybclient", FLAGS_pggate_ybclient_reactor_threads, metric_entity_, mem_tracker_))),
      proxy_cache_(std::make_unique<rpc::ProxyCache>(messenger_holder_.messenger.get())),
      pg_callbacks_(callbacks),
      wait_event_watcher_(
          [starter = pg_callbacks_.PgstatReportWaitStart](
              ash::WaitStateCode wait_event, ash::PggateRPC pggate_rpc) {
            return PgWaitEventWatcher{starter, wait_event, pggate_rpc};
      }),
      pg_shared_data_(
          *init_postgres_info.shared_data, !init_postgres_info.parallel_leader_session_id),
      pg_client_(
          wait_event_watcher_, pg_shared_data_->next_perform_op_serial_no),
      interrupter_(new Interrupter(*messenger_holder_.messenger, pg_client_)),
      clock_(new server::HybridClock()),
      // For parallel query, multiple PgTxnManager(s) make parallel requests to pg_client_session
      // projecting as a single ysql backend. When object locking is enabled, only the leader worker
      // should acquire object locks and issue finish transaction rpcs to ensure correctness.
      enable_table_locking_(
          ShouldEnableTableLocks() && !init_postgres_info.parallel_leader_session_id),
      pg_txn_manager_(new PgTxnManager(&pg_client_, clock_, pg_callbacks_, enable_table_locking_)),
      pg_session_(make_scoped_refptr<PgSession>(
          pg_client_, pg_txn_manager_, pg_callbacks_, session_stats, is_binary_upgrade,
          wait_event_watcher_, buffering_settings_)),
      fk_reference_cache_(pg_session_, buffering_settings_),
      explicit_row_lock_buffer_(pg_session_) {
  PgBackendSetupSharedMemory();
  // This is an RCU object, but there are no concurrent updates on PG side, only on tserver, so
  // it's safe to just save the pointer.
  tserver_shared_object_ = PgSharedMemoryManager().SharedData().get();

  std::memcpy(ash_config.top_level_node_id, tserver_shared_object_->tserver_uuid(), kUuidSize);
  wait_state_ = ash::WaitStateInfo::CreateIfAshIsEnabled<ash::PgWaitStateInfo>(ash_config);
  ash::WaitStateInfo::SetCurrentWaitState(wait_state_);
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

//--------------------------------------------------------------------------------------------------

Status PgApiImpl::InvalidateCache(uint64_t min_ysql_catalog_version) {
  pg_session_->InvalidateAllTablesCache(min_ysql_catalog_version);
  return Status::OK();
}

Status PgApiImpl::UpdateTableCacheMinVersion(uint64_t min_ysql_catalog_version) {
  pg_session_->UpdateTableCacheMinVersion(min_ysql_catalog_version);
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
  return pg_client_.CreateSequencesDataTable();
}

Status PgApiImpl::InsertSequenceTuple(
    int64_t db_oid, int64_t seq_oid, uint64_t ysql_catalog_version, bool is_db_catalog_version_mode,
    int64_t last_val, bool is_called) {
  return pg_client_.InsertSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, is_db_catalog_version_mode, last_val, is_called);
}

Status PgApiImpl::UpdateSequenceTupleConditionally(
    int64_t db_oid, int64_t seq_oid, uint64_t ysql_catalog_version, bool is_db_catalog_version_mode,
    int64_t last_val, bool is_called, int64_t expected_last_val, bool expected_is_called,
    bool *skipped) {
  *skipped = VERIFY_RESULT(pg_client_.UpdateSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, is_db_catalog_version_mode, last_val, is_called,
      expected_last_val, expected_is_called));
  return Status::OK();
}

Status PgApiImpl::UpdateSequenceTuple(
    int64_t db_oid, int64_t seq_oid, uint64_t ysql_catalog_version, bool is_db_catalog_version_mode,
    int64_t last_val, bool is_called, bool* skipped) {
  auto result = VERIFY_RESULT(pg_client_.UpdateSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, is_db_catalog_version_mode, last_val,
      is_called, std::nullopt, std::nullopt));
  if (skipped) {
    *skipped = result;
  }
  return Status::OK();
}

Status PgApiImpl::FetchSequenceTuple(
    int64_t db_oid, int64_t seq_oid, uint64_t ysql_catalog_version, bool is_db_catalog_version_mode,
    uint32_t fetch_count, int64_t inc_by, int64_t min_value, int64_t max_value, bool cycle,
    int64_t *first_value, int64_t *last_value) {
  auto res = VERIFY_RESULT(pg_client_.FetchSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, is_db_catalog_version_mode, fetch_count, inc_by,
      min_value, max_value, cycle));
  *first_value = res.first;
  *last_value = res.second;
  return Status::OK();
}

Status PgApiImpl::ReadSequenceTuple(
    int64_t db_oid, int64_t seq_oid, uint64_t ysql_catalog_version, bool is_db_catalog_version_mode,
    int64_t *last_val, bool *is_called) {
  const auto actual_ysql_catalog_version =
      yb_disable_catalog_version_check ? std::nullopt : std::optional(ysql_catalog_version);
  const auto actual_yb_read_time = yb_read_time ? std::optional(yb_read_time) : std::nullopt;
  const auto res = VERIFY_RESULT(pg_client_.ReadSequenceTuple(
      db_oid, seq_oid, actual_ysql_catalog_version, is_db_catalog_version_mode,
      actual_yb_read_time));
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

Status PgApiImpl::IsDatabaseColocated(const PgOid database_oid, bool *colocated,
                                      bool *legacy_colocated_database) {
  return pg_session_->IsDatabaseColocated(database_oid, colocated, legacy_colocated_database);
}

Status PgApiImpl::NewCreateDatabase(
    const char* database_name, PgOid database_oid, PgOid source_database_oid, PgOid next_oid,
    bool colocated, YbcCloneInfo* yb_clone_info, PgStatement **handle) {
  return AddToCurrentPgMemctx(
      std::make_unique<PgCreateDatabase>(
          pg_session_, database_name, database_oid, source_database_oid, next_oid, yb_clone_info,
          colocated, pg_txn_manager_->IsDdlMode(),
          pg_txn_manager_->IsDdlModeWithRegularTransactionBlock()),
      handle);
}

Status PgApiImpl::ExecCreateDatabase(PgStatement* handle) {
  return VERIFY_RESULT_REF(GetStatementAs<PgCreateDatabase>(handle)).Exec();
}

Status PgApiImpl::NewDropDatabase(
    const char* database_name, PgOid database_oid, PgStatement** handle) {
  return AddToCurrentPgMemctx(
      std::make_unique<PgDropDatabase>(pg_session_, database_name, database_oid), handle);
}

Status PgApiImpl::ExecDropDatabase(PgStatement* handle) {
  return VERIFY_RESULT_REF(GetStatementAs<PgDropDatabase>(handle)).Exec();
}

Status PgApiImpl::NewAlterDatabase(
    const char* database_name, PgOid database_oid, PgStatement** handle) {
  return AddToCurrentPgMemctx(
    std::make_unique<PgAlterDatabase>(pg_session_, database_name, database_oid), handle);
}

Status PgApiImpl::AlterDatabaseRenameDatabase(PgStatement* handle, const char* newname) {
  VERIFY_RESULT_REF(GetStatementAs<PgAlterDatabase>(handle)).RenameDatabase(newname);
  return Status::OK();
}

Status PgApiImpl::ExecAlterDatabase(PgStatement* handle) {
  return VERIFY_RESULT_REF(GetStatementAs<PgAlterDatabase>(handle)).Exec();
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

// This function is only used to get the protobuf-based catalog version, not using
// the pg_yb_catalog_version table.
Status PgApiImpl::GetCatalogMasterVersion(uint64_t *version) {
  *version = VERIFY_RESULT(pg_client_.GetCatalogMasterVersion());
  return Status::OK();
}

Status PgApiImpl::CancelTransaction(const unsigned char* transaction_id) {
  return pg_client_.CancelTransaction(transaction_id);
}

Result<PgTableDescPtr> PgApiImpl::LoadTable(const PgObjectId& table_id) {
  return pg_session_->LoadTable(table_id);
}

void PgApiImpl::InvalidateTableCache(const PgObjectId& table_id) {
  pg_session_->InvalidateTableCache(table_id, InvalidateOnPgClient::kTrue);
}

void PgApiImpl::RemoveTableCacheEntry(const PgObjectId& table_id) {
  pg_session_->InvalidateTableCache(table_id, InvalidateOnPgClient::kFalse);
}

//--------------------------------------------------------------------------------------------------

Status PgApiImpl::NewCreateTablegroup(
    const char* database_name, PgOid database_oid, PgOid tablegroup_oid, PgOid tablespace_oid,
    PgStatement** handle) {
  SCHECK(pg_txn_manager_->IsDdlMode(),
         IllegalState,
         "Tablegroup is being created outside of DDL mode");
  return AddToCurrentPgMemctx(
      std::make_unique<PgCreateTablegroup>(
          pg_session_, database_name, database_oid, tablegroup_oid, tablespace_oid,
          pg_txn_manager_->IsDdlModeWithRegularTransactionBlock()),
      handle);
}

Status PgApiImpl::ExecCreateTablegroup(PgStatement* handle) {
  return ExecDdlWithSyscatalogChanges<PgCreateTablegroup>(handle, *pg_session_);
}

Status PgApiImpl::NewDropTablegroup(
    PgOid database_oid, PgOid tablegroup_oid, PgStatement** handle) {
  SCHECK(pg_txn_manager_->IsDdlMode(),
         IllegalState,
         "Tablegroup is being dropped outside of DDL mode");
  return AddToCurrentPgMemctx(
      std::make_unique<PgDropTablegroup>(
          pg_session_, database_oid, tablegroup_oid,
          pg_txn_manager_->IsDdlModeWithRegularTransactionBlock()),
      handle);
}

Status PgApiImpl::ExecDropTablegroup(PgStatement* handle) {
  return ExecDdlWithSyscatalogChanges<PgDropTablegroup>(handle, *pg_session_);
}


//--------------------------------------------------------------------------------------------------

Status PgApiImpl::NewCreateTable(const char* database_name,
                                 const char* schema_name,
                                 const char* table_name,
                                 const PgObjectId& table_id,
                                 bool is_shared_table,
                                 bool is_sys_catalog_table,
                                 bool if_not_exist,
                                 YbcPgYbrowidMode ybrowid_mode,
                                 bool is_colocated_via_database,
                                 const PgObjectId& tablegroup_oid,
                                 const ColocationId colocation_id,
                                 const PgObjectId& tablespace_oid,
                                 bool is_matview,
                                 const PgObjectId& pg_table_oid,
                                 const PgObjectId& old_relfilenode_oid,
                                 bool is_truncate,
                                 PgStatement** handle) {
  return AddToCurrentPgMemctx(
      std::make_unique<PgCreateTable>(
          pg_session_, database_name, schema_name, table_name, table_id, is_shared_table,
          is_sys_catalog_table, if_not_exist, ybrowid_mode, is_colocated_via_database,
          tablegroup_oid, colocation_id, tablespace_oid, is_matview, pg_table_oid,
          old_relfilenode_oid, is_truncate, pg_txn_manager_->IsDdlMode(),
          pg_txn_manager_->IsDdlModeWithRegularTransactionBlock()),
      handle);
}

Status PgApiImpl::CreateTableAddColumn(
    PgStatement* handle, const char* attr_name, int attr_num, const YbcPgTypeEntity* attr_type,
    bool is_hash, bool is_range, bool is_desc, bool is_nulls_first) {
  return AddColumn(
      VERIFY_RESULT_REF(GetStatementAs<PgCreateTable>(handle)),
      attr_name, attr_num, attr_type, is_hash, is_range, is_desc, is_nulls_first);
}

Status PgApiImpl::CreateTableSetNumTablets(PgStatement* handle, int32_t num_tablets) {
  return VERIFY_RESULT_REF(GetStatementAs<PgCreateTable>(handle)).SetNumTablets(num_tablets);
}

Status PgApiImpl::AddSplitBoundary(PgStatement* handle, PgExpr** exprs, int expr_count) {
  return VERIFY_RESULT_REF(GetStatementAs<PgCreateTableBase>(handle)).AddSplitBoundary(
    exprs, expr_count);
}

Status PgApiImpl::ExecCreateTable(PgStatement* handle) {
  return ExecDdlWithSyscatalogChanges<PgCreateTable>(handle, *pg_session_);
}

Status PgApiImpl::NewAlterTable(const PgObjectId& table_id, PgStatement** handle) {
  return AddToCurrentPgMemctx(
      std::make_unique<PgAlterTable>(
          pg_session_, table_id, pg_txn_manager_->IsDdlMode(),
          pg_txn_manager_->IsDdlModeWithRegularTransactionBlock()),
      handle);
}

Status PgApiImpl::AlterTableAddColumn(
    PgStatement* handle, const char* name, int order, const YbcPgTypeEntity* attr_type,
    YbcPgExpr missing_value) {
  return VERIFY_RESULT_REF(GetStatementAs<PgAlterTable>(handle)).AddColumn(
      name, attr_type, order, missing_value);
}

Status PgApiImpl::AlterTableRenameColumn(
    PgStatement* handle, const char* oldname, const char* newname) {
  return VERIFY_RESULT_REF(GetStatementAs<PgAlterTable>(handle)).RenameColumn(oldname, newname);
}

Status PgApiImpl::AlterTableDropColumn(PgStatement* handle, const char* name) {
  return VERIFY_RESULT_REF(GetStatementAs<PgAlterTable>(handle)).DropColumn(name);
}

Status PgApiImpl::AlterTableSetReplicaIdentity(PgStatement* handle, char identity_type) {
  return VERIFY_RESULT_REF(GetStatementAs<PgAlterTable>(handle)).SetReplicaIdentity(identity_type);
}

Status PgApiImpl::AlterTableRenameTable(PgStatement* handle, const char* newname) {
  return VERIFY_RESULT_REF(GetStatementAs<PgAlterTable>(handle)).RenameTable(newname);
}

Status PgApiImpl::AlterTableIncrementSchemaVersion(PgStatement* handle) {
  return VERIFY_RESULT_REF(GetStatementAs<PgAlterTable>(handle)).IncrementSchemaVersion();
}

Status PgApiImpl::AlterTableSetTableId(PgStatement* handle, const PgObjectId& table_id) {
  return VERIFY_RESULT_REF(GetStatementAs<PgAlterTable>(handle)).SetTableId(table_id);
}

Status PgApiImpl::AlterTableSetSchema(PgStatement* handle, const char* schema_name) {
  return VERIFY_RESULT_REF(GetStatementAs<PgAlterTable>(handle)).SetSchema(schema_name);
}

Status PgApiImpl::ExecAlterTable(PgStatement* handle) {
  return ExecDdlWithSyscatalogChanges<PgAlterTable>(handle, *pg_session_);
}

Status PgApiImpl::AlterTableInvalidateTableCacheEntry(PgStatement* handle) {
  VERIFY_RESULT_REF(GetStatementAs<PgAlterTable>(handle)).InvalidateTableCacheEntry();
  return Status::OK();
}

Status PgApiImpl::NewDropTable(const PgObjectId& table_id, bool if_exist, PgStatement** handle) {
  SCHECK(pg_txn_manager_->IsDdlMode(),
         IllegalState,
         "Table is being dropped outside of DDL mode");
  return AddToCurrentPgMemctx(
      std::make_unique<PgDropTable>(
          pg_session_, table_id, if_exist, pg_txn_manager_->IsDdlModeWithRegularTransactionBlock()),
      handle);
}

Status PgApiImpl::NewTruncateTable(const PgObjectId& table_id, PgStatement** handle) {
  return AddToCurrentPgMemctx(
      std::make_unique<PgTruncateTable>(pg_session_, table_id), handle);
}

Status PgApiImpl::ExecTruncateTable(PgStatement* handle) {
  return VERIFY_RESULT_REF(GetStatementAs<PgTruncateTable>(handle)).Exec();
}

Status PgApiImpl::NewDropSequence(PgOid database_oid, PgOid sequence_oid, PgStatement** handle) {
  return AddToCurrentPgMemctx(
      std::make_unique<PgDropSequence>(pg_session_, database_oid, sequence_oid), handle);
}

Status PgApiImpl::ExecDropSequence(PgStatement* handle) {
  return VERIFY_RESULT_REF(GetStatementAs<PgDropSequence>(handle)).Exec();
}

Status PgApiImpl::NewDropDBSequences(PgOid database_oid, PgStatement** handle) {
  return AddToCurrentPgMemctx(
      std::make_unique<PgDropDBSequences>(pg_session_, database_oid), handle);
}

Status PgApiImpl::GetTableDesc(const PgObjectId& table_id, PgTableDesc **handle) {
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

Result<tserver::PgListClonesResponsePB> PgApiImpl::GetDatabaseClones() {
  return pg_client_.ListDatabaseClones();
}

Result<YbcPgColumnInfo> PgApiImpl::GetColumnInfo(YbcPgTableDesc table_desc, int16_t attr_number) {
  return table_desc->GetColumnInfo(attr_number);
}

Result<bool> PgApiImpl::DmlModifiesRow(PgStatement* handle) {
  auto& dml = VERIFY_RESULT_REF(GetStatementAs<PgDml>(handle));

  switch (dml.stmt_op()) {
    case PgUpdate::kStmtOp:
    case PgDelete::kStmtOp:
      return true;
    default:
      break;
  }

  return false;
}

Status PgApiImpl::SetIsSysCatalogVersionChange(PgStatement* handle) {
  VERIFY_RESULT_REF(GetStatementAs<PgDmlWrite>(
      handle,
      {
          StatementTag<PgInsert>(),
          StatementTag<PgUpdate>(),
          StatementTag<PgDelete>()
      })).SetIsSystemCatalogChange();
  return Status::OK();
}

Status PgApiImpl::SetCatalogCacheVersion(
    PgStatement* handle, uint64_t version, std::optional<PgOid> db_oid) {
  VERIFY_RESULT_REF(GetStatementAs<PgDml>(handle, {
          StatementTag<PgInsert>(),
          StatementTag<PgUpdate>(),
          StatementTag<PgDelete>(),
          StatementTag<PgSelect>()
      })).SetCatalogCacheVersion(db_oid, version);
  return Status::OK();
}

Status PgApiImpl::SetTablespaceOid(
    PgStatement* handle, uint32_t tablespace_oid) {
  VERIFY_RESULT_REF(GetStatementAs<PgDml>(handle, {
          StatementTag<PgInsert>(),
          StatementTag<PgUpdate>(),
          StatementTag<PgDelete>(),
          StatementTag<PgSelect>()
      })).SetTablespaceOid(tablespace_oid);
  return Status::OK();
}

Result<client::TableSizeInfo> PgApiImpl::GetTableDiskSize(const PgObjectId& table_oid) {
  return pg_client_.GetTableDiskSize(table_oid);
}

//--------------------------------------------------------------------------------------------------

Status PgApiImpl::NewCreateIndex(const char* database_name,
                                 const char* schema_name,
                                 const char* index_name,
                                 const PgObjectId& index_id,
                                 const PgObjectId& base_table_id,
                                 bool is_shared_index,
                                 bool is_sys_catalog_index,
                                 bool is_unique_index,
                                 bool skip_index_backfill,
                                 bool if_not_exist,
                                 bool is_colocated_via_database,
                                 const PgObjectId& tablegroup_oid,
                                 const YbcPgOid& colocation_id,
                                 const PgObjectId& tablespace_oid,
                                 const PgObjectId& pg_table_id,
                                 const PgObjectId& old_relfilenode_id,
                                 PgStatement** handle) {
  return AddToCurrentPgMemctx(
      std::make_unique<PgCreateIndex>(
          pg_session_, database_name, schema_name, index_name, index_id, is_shared_index,
          is_sys_catalog_index, if_not_exist, PG_YBROWID_MODE_NONE, is_colocated_via_database,
          tablegroup_oid, colocation_id, tablespace_oid, false /* is_matview */, pg_table_id,
          old_relfilenode_id, false /* is_truncate */, pg_txn_manager_->IsDdlMode(),
          pg_txn_manager_->IsDdlModeWithRegularTransactionBlock(), base_table_id,
          is_unique_index, skip_index_backfill),
      handle);
}

Status PgApiImpl::CreateIndexAddColumn(
    PgStatement* handle, const char* attr_name, int attr_num, const YbcPgTypeEntity* attr_type,
    bool is_hash, bool is_range, bool is_desc, bool is_nulls_first) {
  return AddColumn(
      VERIFY_RESULT_REF(GetStatementAs<PgCreateIndex>(handle)),
      attr_name, attr_num, attr_type, is_hash, is_range, is_desc, is_nulls_first);
}

Status PgApiImpl::CreateIndexSetNumTablets(PgStatement* handle, int32_t num_tablets) {
  return VERIFY_RESULT_REF(GetStatementAs<PgCreateIndex>(handle)).SetNumTablets(num_tablets);
}

Status PgApiImpl::CreateIndexSetVectorOptions(PgStatement* handle, YbcPgVectorIdxOptions* options) {
  return VERIFY_RESULT_REF(GetStatementAs<PgCreateIndex>(handle)).SetVectorOptions(options);
}

Status PgApiImpl::CreateIndexSetHnswOptions(
    PgStatement* handle, int m, int m0, int ef_construction) {
  return VERIFY_RESULT_REF(GetStatementAs<PgCreateIndex>(handle))
      .SetHnswOptions(m, m0, ef_construction);
}

Status PgApiImpl::ExecCreateIndex(PgStatement* handle) {
  return ExecDdlWithSyscatalogChanges<PgCreateIndex>(handle, *pg_session_);
}

Status PgApiImpl::NewDropIndex(
    const PgObjectId& index_id, bool if_exist, bool ddl_rollback_enabled, PgStatement** handle) {
  SCHECK(pg_txn_manager_->IsDdlMode(), IllegalState, "Index is being dropped outside of DDL mode");
  return AddToCurrentPgMemctx(
      std::make_unique<PgDropIndex>(
          pg_session_, index_id, if_exist, ddl_rollback_enabled,
          pg_txn_manager_->IsDdlModeWithRegularTransactionBlock()),
      handle);
}

Status PgApiImpl::ExecPostponedDdlStmt(PgStatement* handle) {
  auto& ddl = VERIFY_RESULT_REF(GetStatementAs<PgDdl>(
      handle,
      {
          StatementTag<PgDropTable>(),
          StatementTag<PgDropIndex>(),
          StatementTag<PgDropTablegroup>(),
          StatementTag<PgDropSequence>(),
          StatementTag<PgDropDBSequences>()
      }));

  const auto stmt_op = ddl.stmt_op();

#define YB_EXECUTE_DDL_STMT_CASE(name) \
  case name::kStmtOp: return down_cast<name&>(ddl).Exec()

  switch (stmt_op) {
    YB_EXECUTE_DDL_STMT_CASE(PgDropTable);
    YB_EXECUTE_DDL_STMT_CASE(PgDropIndex);
    YB_EXECUTE_DDL_STMT_CASE(PgDropTablegroup);
    YB_EXECUTE_DDL_STMT_CASE(PgDropSequence);
    YB_EXECUTE_DDL_STMT_CASE(PgDropDBSequences);

    default:
      break;
  }

#undef YB_EXECUTE_PG_STMT

  RSTATUS_DCHECK(false, IllegalState, "Unexpected stmt_op $0", std::to_underlying(stmt_op));
  return Status::OK();
}

Status PgApiImpl::ExecDropTable(PgStatement* handle) {
  return ExecDdlWithSyscatalogChanges<PgDropTable>(handle, *pg_session_);
}

Status PgApiImpl::ExecDropIndex(PgStatement* handle) {
  return ExecDdlWithSyscatalogChanges<PgDropIndex>(handle, *pg_session_);
}

Result<int> PgApiImpl::WaitForBackendsCatalogVersion(PgOid dboid, uint64_t version, pid_t pid) {
  tserver::PgWaitForBackendsCatalogVersionRequestPB req;
  req.set_database_oid(dboid);
  req.set_catalog_version(version);
  req.set_requestor_pg_backend_pid(pid);
  // Incorporate the margin into the deadline because master will subtract the margin for
  // responding.
  return pg_client_.WaitForBackendsCatalogVersion(
      &req,
      CoarseMonoClock::Now() + MonoDelta::FromMilliseconds(
        FLAGS_wait_for_ysql_backends_catalog_version_client_master_rpc_timeout_ms
        + FLAGS_wait_for_ysql_backends_catalog_version_client_master_rpc_margin_ms));
}

Status PgApiImpl::BackfillIndex(const PgObjectId& table_id) {
  tserver::PgBackfillIndexRequestPB req;
  table_id.ToPB(req.mutable_table_id());
  return pg_client_.BackfillIndex(
      &req, CoarseMonoClock::Now() + FLAGS_backfill_index_client_rpc_timeout_ms * 1ms);
}

Status PgApiImpl::WaitVectorIndexReady(const PgObjectId& table_id) {
  while (!VERIFY_RESULT(pg_client_.PollVectorIndexReady(table_id))) {}
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// DML Statement Support.
//--------------------------------------------------------------------------------------------------

Status PgApiImpl::DmlAppendTarget(
    PgStatement* handle, PgExpr* target, bool is_for_secondary_index) {
  return VERIFY_RESULT_REF(
      GetStatementAs<PgDml>(handle)).AppendTarget(target, is_for_secondary_index);
}

Status PgApiImpl::DmlAppendQual(
    PgStatement* handle, PgExpr* qual, uint32_t serialization_version,
    bool is_for_secondary_index) {
  return VERIFY_RESULT_REF(
      GetStatementAs<PgDmlRead>(handle)).AppendQual(
          qual, serialization_version, is_for_secondary_index);
}

Status PgApiImpl::DmlAppendColumnRef(
    PgStatement* handle, PgColumnRef* colref, bool is_for_secondary_index) {
  return VERIFY_RESULT_REF(
      GetStatementAs<PgDml>(handle)).AppendColumnRef(colref, is_for_secondary_index);
}

Status PgApiImpl::DmlBindColumn(PgStatement* handle, int attr_num, PgExpr* attr_value) {
  return VERIFY_RESULT_REF(GetStatementAs<PgDml>(handle)).BindColumn(attr_num, attr_value);
}

Status PgApiImpl::DmlBindRow(
    PgStatement* handle, uint64_t ybctid, YbcBindColumn* columns, int count) {
  return VERIFY_RESULT_REF(GetStatementAs<PgDmlWrite>(handle)).BindRow(ybctid, columns, count);
}

Status PgApiImpl::DmlBindColumnCondBetween(
    PgStatement* handle, int attr_num, PgExpr* attr_value, bool start_inclusive,
    PgExpr* attr_value_end, bool end_inclusive) {
  return VERIFY_RESULT_REF(GetStatementAs<PgDmlRead>(handle)).BindColumnCondBetween(
      attr_num, attr_value, start_inclusive, attr_value_end, end_inclusive);
}

Status PgApiImpl::DmlBindColumnCondIsNotNull(PgStatement* handle, int attr_num) {
  return VERIFY_RESULT_REF(GetStatementAs<PgDmlRead>(handle)).BindColumnCondIsNotNull(attr_num);
}

Status PgApiImpl::DmlBindColumnCondIn(
    PgStatement* handle, YbcPgExpr lhs, int n_attr_values, PgExpr** attr_values) {
  return VERIFY_RESULT_REF(GetStatementAs<PgDmlRead>(handle)).BindColumnCondIn(
      lhs, n_attr_values, attr_values);
}

Status PgApiImpl::DmlAddRowUpperBound(
    PgStatement* handle, int n_col_values, PgExpr** col_values, bool is_inclusive) {
  return VERIFY_RESULT_REF(GetStatementAs<PgDmlRead>(handle)).AddRowUpperBound(
      handle, n_col_values, col_values, is_inclusive);
}

Status PgApiImpl::DmlAddRowLowerBound(
    PgStatement* handle, int n_col_values, PgExpr** col_values, bool is_inclusive) {
  return VERIFY_RESULT_REF(GetStatementAs<PgDmlRead>(handle)).AddRowLowerBound(
      handle, n_col_values, col_values, is_inclusive);
}

Status PgApiImpl::DmlBindHashCode(
    PgStatement* handle, const std::optional<Bound>& start, const std::optional<Bound>& end) {
  VERIFY_RESULT_REF(GetStatementAs<PgDmlRead>(handle)).BindHashCode(start, end);
  return Status::OK();
}

Status PgApiImpl::DmlBindRange(
    PgStatement* handle, Slice lower_bound, bool lower_bound_inclusive, Slice upper_bound,
    bool upper_bound_inclusive) {
  return VERIFY_RESULT_REF(GetStatementAs<PgDmlRead>(handle)).BindRange(
      lower_bound, lower_bound_inclusive, upper_bound, upper_bound_inclusive);
}

Status PgApiImpl::DmlBindBounds(
    PgStatement* handle, const Slice lower_bound, bool lower_bound_inclusive,
    const Slice upper_bound, bool upper_bound_inclusive) {
  VERIFY_RESULT_REF(GetStatementAs<PgDmlRead>(handle))
      .BindBounds(lower_bound, lower_bound_inclusive, upper_bound, upper_bound_inclusive);
  return Status::OK();
}

Status PgApiImpl::DmlSetMergeSortKeys(YbcPgStatement handle, int num_keys,
                                      const YbcSortKey *sort_keys) {
  return VERIFY_RESULT_REF(GetStatementAs<PgDmlRead>(handle)).SetMergeSortKeys(num_keys, sort_keys);
}

Status PgApiImpl::DmlBindTable(PgStatement* handle) {
  return VERIFY_RESULT_REF(GetStatementAs<PgDml>(handle)).BindTable();
}

Result<YbcPgColumnInfo> PgApiImpl::DmlGetColumnInfo(PgStatement* handle, int attr_num) {
  return VERIFY_RESULT_REF(GetStatementAs<PgDml>(handle)).GetColumnInfo(attr_num);
}

Status PgApiImpl::DmlAssignColumn(PgStatement* handle, int attr_num, PgExpr* attr_value) {
  return VERIFY_RESULT_REF(GetStatementAs<PgDml>(handle)).AssignColumn(attr_num, attr_value);
}

Status PgApiImpl::DmlFetch(
    PgStatement* handle, int32_t natts, uint64_t* values, bool* isnulls, YbcPgSysColumns* syscols,
    bool* has_data) {
  return VERIFY_RESULT_REF(GetStatementAs<PgDml>(handle)).Fetch(
      natts, values, isnulls, syscols, has_data);
}

Result<dockv::KeyBytes> PgApiImpl::BuildTupleId(const YbcPgYBTupleIdDescriptor& descr) {
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

Status PgApiImpl::FlushBufferedOperations(const PgFlushDebugContext& dbg_ctx) {
  return ResultToStatus(pg_session_->FlushBufferedOperations(dbg_ctx));
}

Status PgApiImpl::AdjustOperationsBuffering(int multiple) {
  return pg_session_->AdjustOperationsBuffering(multiple);
}

Status PgApiImpl::DmlExecWriteOp(PgStatement *handle, int32_t *rows_affected_count) {
  auto& dml_write = VERIFY_RESULT_REF(GetStatementAs<PgDmlWrite>(handle));
  RETURN_NOT_OK(dml_write.Exec(ForceNonBufferable{rows_affected_count != nullptr}));
  if (rows_affected_count) {
    *rows_affected_count = dml_write.GetRowsAffectedCount();
  }
  return Status::OK();
}

// Insert ------------------------------------------------------------------------------------------

Result<PgStatement*> PgApiImpl::NewInsertBlock(
    const PgObjectId& table_id,
    const YbcPgTableLocalityInfo& locality_info,
    YbcPgTransactionSetting transaction_setting) {
  if (!FLAGS_ysql_pack_inserted_value) {
    return nullptr;
  }

  PgStatement *result = nullptr;
  RETURN_NOT_OK(AddToCurrentPgMemctx(
      VERIFY_RESULT(PgInsert::Make(
          pg_session_, table_id, locality_info, transaction_setting, /* packed= */ true)),
      &result));
  return result;
}

Status PgApiImpl::NewInsert(
    const PgObjectId& table_id, const YbcPgTableLocalityInfo& locality_info, PgStatement **handle,
    YbcPgTransactionSetting transaction_setting) {
  *handle = nullptr;
  return AddToCurrentPgMemctx(
    VERIFY_RESULT(PgInsert::Make(
        pg_session_, table_id, locality_info, transaction_setting, /* packed= */ false)),
    handle);
}

Status PgApiImpl::ExecInsert(PgStatement* handle) {
  return VERIFY_RESULT_REF(GetStatementAs<PgInsert>(handle)).Exec();
}

Status PgApiImpl::InsertStmtSetUpsertMode(PgStatement* handle) {
  VERIFY_RESULT_REF(GetStatementAs<PgInsert>(handle)).SetUpsertMode();
  return Status::OK();
}

Status PgApiImpl::InsertStmtSetWriteTime(PgStatement* handle, HybridTime write_time) {
  return VERIFY_RESULT_REF(GetStatementAs<PgInsert>(handle)).SetWriteTime(write_time);
}

Status PgApiImpl::InsertStmtSetIsBackfill(PgStatement* handle, bool is_backfill) {
  VERIFY_RESULT_REF(GetStatementAs<PgInsert>(handle)).SetIsBackfill(is_backfill);
  return Status::OK();
}

// Update ------------------------------------------------------------------------------------------

Status PgApiImpl::NewUpdate(
    const PgObjectId& table_id, const YbcPgTableLocalityInfo& locality_info, PgStatement** handle,
    YbcPgTransactionSetting transaction_setting) {
  *handle = nullptr;
  return AddToCurrentPgMemctx(
      VERIFY_RESULT(PgUpdate::Make(pg_session_, table_id, locality_info, transaction_setting)),
      handle);
}

Status PgApiImpl::ExecUpdate(PgStatement* handle) {
  return VERIFY_RESULT_REF(GetStatementAs<PgUpdate>(handle)).Exec();
}

// Delete ------------------------------------------------------------------------------------------

Status PgApiImpl::NewDelete(
    const PgObjectId& table_id, const YbcPgTableLocalityInfo& locality_info, PgStatement** handle,
    YbcPgTransactionSetting transaction_setting) {
  *handle = nullptr;
  return AddToCurrentPgMemctx(
      VERIFY_RESULT(PgDelete::Make(pg_session_, table_id, locality_info, transaction_setting)),
      handle);
}

Status PgApiImpl::ExecDelete(PgStatement* handle) {
  return VERIFY_RESULT_REF(GetStatementAs<PgDelete>(handle)).Exec();
}

Status PgApiImpl::NewSample(
    const PgObjectId& table_id, const YbcPgTableLocalityInfo& locality_info, int targrows,
    const SampleRandomState& rand_state, PgStatement** handle) {
  *handle = nullptr;
  return AddToCurrentPgMemctx(
      VERIFY_RESULT(PgSample::Make(
          pg_session_, table_id, locality_info, targrows, rand_state, clock_->Now())),
      handle);
}

Result<bool> PgApiImpl::SampleNextBlock(PgStatement* handle) {
  return VERIFY_RESULT_REF(GetStatementAs<PgSample>(handle)).SampleNextBlock();
}

Status PgApiImpl::ExecSample(PgStatement* handle, YbcPgExecParameters* exec_params) {
  auto& sample = VERIFY_RESULT_REF(GetStatementAs<PgSample>(handle));
  RETURN_NOT_OK(sample.SetNextBatchYbctids(exec_params));
  return sample.Exec(exec_params);
}

Result<EstimatedRowCount> PgApiImpl::GetEstimatedRowCount(PgStatement* handle) {
  return VERIFY_RESULT_REF(GetStatementAs<PgSample>(handle)).GetEstimatedRowCount();
}

Status PgApiImpl::DeleteStmtSetIsPersistNeeded(PgStatement* handle, bool is_persist_needed) {
  VERIFY_RESULT_REF(GetStatementAs<PgDelete>(handle)).SetIsPersistNeeded(is_persist_needed);
  return Status::OK();
}

// Colocated Truncate ------------------------------------------------------------------------------

Status PgApiImpl::NewTruncateColocated(
    const PgObjectId& table_id, const YbcPgTableLocalityInfo& locality_info, PgStatement** handle,
    YbcPgTransactionSetting transaction_setting) {
  *handle = nullptr;
  return AddToCurrentPgMemctx(
      VERIFY_RESULT(PgTruncateColocated::Make(
          pg_session_, table_id, locality_info, transaction_setting)),
      handle);
}

Status PgApiImpl::ExecTruncateColocated(PgStatement* handle) {
  return VERIFY_RESULT_REF(GetStatementAs<PgTruncateColocated>(handle)).Exec();
}

// Select ------------------------------------------------------------------------------------------

Status PgApiImpl::NewSelect(
    const PgObjectId& table_id, const PgObjectId& index_id,
    const YbcPgPrepareParameters* prepare_params, const YbcPgTableLocalityInfo& locality_info,
    PgStatement** handle) {
  DCHECK(index_id.IsValid() || table_id.IsValid());
  DCHECK(!(prepare_params && prepare_params->index_only_scan) || index_id.IsValid());

  *handle = nullptr;
  return AddToCurrentPgMemctx(
      VERIFY_RESULT(MakeSelectStatement(
          pg_session_, table_id, index_id, prepare_params, locality_info)),
      handle);
}

Status PgApiImpl::SetForwardScan(PgStatement* handle, bool is_forward_scan) {
  VERIFY_RESULT_REF(GetStatementAs<PgSelect>(handle)).SetForwardScan(is_forward_scan);
  return Status::OK();
}

Status PgApiImpl::SetDistinctPrefixLength(PgStatement* handle, int distinct_prefix_length) {
  VERIFY_RESULT_REF(GetStatementAs<PgSelect>(handle)).SetDistinctPrefixLength(
      distinct_prefix_length);
  return Status::OK();
}

Result<bool> PgApiImpl::RetrieveYbctids(
    PgStatement* handle, const YbcPgExecParameters* exec_params, int natts, YbcSliceVector* ybctids,
    size_t* count) {
  auto& select = VERIFY_RESULT_REF(GetStatementAs<PgSelect>(handle));
  RETURN_NOT_OK(select.Exec(exec_params));
  const auto max_mem_bytes = exec_params->work_mem * 1024L;
  auto vec = std::make_unique<std::vector<Slice>>();
  if (!VERIFY_RESULT(RetrieveYbctidsImpl(pg_types(), select, natts, max_mem_bytes, *vec))) {
    // delete these allocated ybctids, we won't use them
    for (auto ybctid : *vec) {
      delete[] ybctid.cdata();
    }
    return false;
  }
  *count = vec->size();
  *ybctids = vec.release();
  return true;
}

Status PgApiImpl::FetchRequestedYbctids(
    PgStatement* handle, const YbcPgExecParameters* exec_params, YbcConstSliceVector ybctids) {
  auto& select = VERIFY_RESULT_REF(GetStatementAs<PgSelect>(handle));
  select.SetRequestedYbctids(*pointer_cast<const std::vector<Slice>*>(ybctids));
  return select.Exec(exec_params);
}

Status PgApiImpl::BindYbctids(PgStatement* handle, int n, uintptr_t* ybctids) {
  const auto sz = yb::make_unsigned(n);
  const auto ybctids_span = std::span{ybctids, sz};
  VERIFY_RESULT_REF(GetStatementAs<PgSelect>(handle)).SetRequestedYbctids(
      {
        make_lw_function([this, i = ybctids_span.begin(), end = ybctids_span.end()] mutable {
          return i != end ? YbctidAsSlice(pg_types_, *i++) : Slice();
        }), sz});
  return Status::OK();
}

bool PgApiImpl::IsValidYbctid(uint64_t ybctid) {
  dockv::DocKey key;
  auto s = key.DecodeFrom(YbctidAsSlice(pg_types_, ybctid));
  return s.ok();
}

Status PgApiImpl::DmlANNBindVector(PgStatement* handle, PgExpr* vector) {
  return VERIFY_RESULT_REF(GetStatementAs<PgDml>(handle)).ANNBindVector(vector);
}

Status PgApiImpl::DmlANNSetPrefetchSize(PgStatement* handle, int prefetch_size) {
  return VERIFY_RESULT_REF(GetStatementAs<PgDml>(handle)).ANNSetPrefetchSize(prefetch_size);
}

Status PgApiImpl::DmlHnswSetReadOptions(PgStatement* handle, int ef_search) {
  return VERIFY_RESULT_REF(GetStatementAs<PgDml>(handle)).HnswSetReadOptions(ef_search);
}

Status PgApiImpl::ExecSelect(PgStatement* handle, const YbcPgExecParameters* exec_params) {
  auto& select = VERIFY_RESULT_REF(GetStatementAs<PgSelect>(handle));
  auto* read_req = select.read_req();
  if (pg_sys_table_prefetcher_ && select.IsReadFromYsqlCatalog() && read_req) {
    // In case of sys tables prefetching is enabled all reads from sys table must use cached data.
    auto data = pg_sys_table_prefetcher_->GetData(*read_req, select.IsIndexOrderedScan());
    if (std::holds_alternative<PrefetchedDataHolder>(data)) {
      select.UpgradeDocOp(MakeDocReadOpWithData(
          pg_session_, std::move(std::get<PrefetchedDataHolder>(data))));
    } else {
      LOG(WARNING) << "Data was not prefetched for table " << read_req->table_id();
      VLOG(5) << "Non prefetched request is: " << read_req->ShortDebugString();
      DCHECK(std::holds_alternative<MissedPrefetchedDataAlternativeReadTime>(data));
      const auto& alternative_read_time = std::get<MissedPrefetchedDataAlternativeReadTime>(data);
      auto catalog_read_time_guard = alternative_read_time
          ? UpdateCatalogReadTime(*pg_session_, *alternative_read_time) : std::nullopt;
      return select.Exec(exec_params);
    }
  }
  return select.Exec(exec_params);
}

void PgApiImpl::IncrementIndexRecheckCount() {
  pg_session_->metrics().RecordRowRemovedByIndexRecheck();
}


//--------------------------------------------------------------------------------------------------
// Functions.
//--------------------------------------------------------------------------------------------------

Status PgApiImpl::NewSRF(PgFunction** handle, PgFunctionDataProcessor processor) {
  *handle = nullptr;
  return AddToCurrentPgMemctx(
      std::make_unique<PgFunction>(std::move(processor), pg_session_), handle);
}

Status PgApiImpl::AddFunctionParam(
    PgFunction *handle, const std::string& name, const YbcPgTypeEntity *type_entity, uint64_t datum,
    bool is_null) {
  if (!handle) {
    return STATUS(InvalidArgument, "Invalid function handle");
  }

  return handle->AddParam(name, type_entity, datum, is_null);
}

Status PgApiImpl::AddFunctionTarget(
    PgFunction *handle, const std::string& name, const YbcPgTypeEntity *type_entity,
    const YbcPgTypeAttrs type_attrs) {
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
    PgStatement* stmt, int attr_num, const YbcPgTypeEntity* type_entity,
    bool collate_is_valid_non_c, const YbcPgTypeAttrs* type_attrs, PgExpr** expr_handle) {
  *expr_handle = PgColumnRef::Create(
     &VERIFY_RESULT_REF(GetArena(stmt)), attr_num, type_entity, collate_is_valid_non_c, type_attrs);
  return Status::OK();
}

// Constant ----------------------------------------------------------------------------------------
Status PgApiImpl::NewConstant(
    PgStatement* stmt, const YbcPgTypeEntity* type_entity, bool collate_is_valid_non_c,
    const char* collation_sortkey, uint64_t datum, bool is_null, YbcPgExpr* expr_handle) {
  auto& arena = VERIFY_RESULT_REF(GetArena(stmt));
  *expr_handle = arena.NewObject<PgConstant>(
      &arena, type_entity, collate_is_valid_non_c, collation_sortkey, datum, is_null);

  return Status::OK();
}

Status PgApiImpl::NewConstantVirtual(
    PgStatement* stmt, const YbcPgTypeEntity* type_entity, YbcPgDatumKind datum_kind,
    YbcPgExpr* expr_handle) {
  auto& arena = VERIFY_RESULT_REF(GetArena(stmt));

  *expr_handle = arena.NewObject<PgConstant>(
      &arena, type_entity, false /* collate_is_valid_non_c */, datum_kind);
  return Status::OK();
}

Status PgApiImpl::NewConstantOp(
    PgStatement* stmt, const YbcPgTypeEntity* type_entity, bool collate_is_valid_non_c,
    const char* collation_sortkey, uint64_t datum, bool is_null, YbcPgExpr* expr_handle,
    bool is_gt) {
  auto& arena = VERIFY_RESULT_REF(GetArena(stmt));
  *expr_handle = arena.NewObject<PgConstant>(
      &arena, type_entity, collate_is_valid_non_c, collation_sortkey, datum, is_null,
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
    PgStatement* stmt, const char* opname, const YbcPgTypeEntity* type_entity,
    bool collate_is_valid_non_c, PgExpr** op_handle) {
  RETURN_NOT_OK(PgExpr::CheckOperatorName(opname));

  *op_handle = PgOperator::Create(
      &VERIFY_RESULT_REF(GetArena(stmt)), opname, type_entity, collate_is_valid_non_c);

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
  return pg_client_.IsInitDbDone();
}

Result<uint64_t> PgApiImpl::GetSharedCatalogVersion(std::optional<PgOid> db_oid) {
  if (!db_oid) {
    return tserver_shared_object_->ysql_catalog_version();
  }
  if (!catalog_version_db_index_) {
    // If db_oid is for a newly created database, it may not have an entry allocated in shared
    // memory. It can also be a race condition case where the database db_oid we are trying to
    // connect to is recently dropped from another node. Let's wait with 500ms interval until the
    // entry shows up or until a 30-second timeout.
    auto status = WaitFor(
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
        30s /* timeout */, Format("Database $0 is not ready in Yugabyte shared memory", *db_oid),
        500ms /* initial_delay */, 1.0 /* delay_multiplier */);

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
    // a 30-second timeout.
    auto status = LoggedWaitFor(
        [this]() -> Result<bool> {
          return tserver_shared_object_->catalog_version_table_in_perdb_mode().has_value();
        },
        30s /* timeout */,
        "catalog_version_table mode not set in shared memory, "
        "tserver not ready to serve requests",
        500ms /* initial_delay */,
        1.0 /* delay_multiplier */);
    RETURN_NOT_OK_PREPEND(
        status,
        "Failed to find out pg_yb_catalog_version mode");
  }
  return tserver_shared_object_->catalog_version_table_in_perdb_mode().value();
}

Result<tserver::PgGetTserverCatalogMessageListsResponsePB>
PgApiImpl::GetTserverCatalogMessageLists(
    uint32_t db_oid, uint64_t ysql_catalog_version, uint32_t num_catalog_versions) {
  return pg_client_.GetTserverCatalogMessageLists(
      db_oid, ysql_catalog_version, num_catalog_versions);
}

Result<tserver::PgSetTserverCatalogMessageListResponsePB>
PgApiImpl::SetTserverCatalogMessageList(
    uint32_t db_oid, bool is_breaking_change, uint64_t new_catalog_version,
    const YbcCatalogMessageList *message_list) {
  std::optional<std::string> messages;
  if (message_list->message_list) {
    messages.emplace(message_list->message_list, message_list->num_bytes);
  }
  return pg_client_.SetTserverCatalogMessageList(
      db_oid, is_breaking_change, new_catalog_version, messages);
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

Result<int> PgApiImpl::GetXClusterRole(uint32_t db_oid) {
  return pg_client_.GetXClusterRole(db_oid);
}

// Tuple Expression -----------------------------------------------------------------------------
Status PgApiImpl::NewTupleExpr(
    PgStatement* stmt, const YbcPgTypeEntity* tuple_type_entity, const YbcPgTypeAttrs* type_attrs,
    int num_elems, const YbcPgExpr* elems, YbcPgExpr* expr_handle) {
  auto& arena = VERIFY_RESULT_REF(GetArena(stmt));

  *expr_handle = arena.NewObject<PgTupleExpr>(
      &arena, tuple_type_entity, type_attrs, num_elems, elems);

  return Status::OK();
}

// Transaction Control -----------------------------------------------------------------------------
Status PgApiImpl::BeginTransaction(int64_t start_time) {
  fk_reference_cache_.Clear();
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

Status PgApiImpl::ResetTransactionReadPoint(bool is_catalog_snapshot) {
  return pg_txn_manager_->ResetTransactionReadPoint(is_catalog_snapshot);
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

Status PgApiImpl::CommitPlainTransaction(const std::optional<PgDdlCommitInfo>& ddl_commit_info) {
  RSTATUS_DCHECK(
      explicit_row_lock_buffer_.IsEmpty(),
      IllegalState, "Expected row lock buffer to be empty");
  RSTATUS_DCHECK(
      pg_session_->IsInsertOnConflictBufferEmpty(),
      IllegalState, "Expected INSERT ... ON CONFLICT buffer to be empty");
  fk_reference_cache_.Clear();

  RETURN_NOT_OK(pg_session_->FlushBufferedOperations(
      PgFlushDebugContext::CommitTxn(
        ddl_commit_info.transform([](const auto& info){ return info.db_oid; }))));
  return pg_txn_manager_->CommitPlainTransaction(ddl_commit_info);
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

Status PgApiImpl::SetDdlStateInPlainTransaction() {
  pg_session_->ResetHasCatalogWriteOperationsInDdlMode();
  return pg_txn_manager_->SetDdlStateInPlainTransaction();
}

Status PgApiImpl::EnterSeparateDdlTxnMode() {
  // Flush all buffered operations as ddl txn use its own transaction session.
  RETURN_NOT_OK(pg_session_->FlushBufferedOperations(PgFlushDebugContext::EnterDdlTxnMode()));
  pg_session_->ResetHasCatalogWriteOperationsInDdlMode();
  return pg_txn_manager_->EnterSeparateDdlTxnMode();
}

bool PgApiImpl::HasWriteOperationsInDdlTxnMode() const {
  return pg_session_->HasCatalogWriteOperationsInDdlMode();
}

Status PgApiImpl::ExitSeparateDdlTxnMode(PgOid db_oid, bool is_silent_modification) {
  // Flush all buffered operations as ddl txn use its own transaction session.
  RETURN_NOT_OK(pg_session_->FlushBufferedOperations(PgFlushDebugContext::ExitDdlTxnMode()));
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
  VLOG_WITH_FUNC(4) << "id: " << id;
  // It's required that we flush all buffered operations before changing the SubTransactionMetadata
  // used by the underlying batcher and RPC logic, as this will snapshot the current
  // SubTransactionMetadata for use in construction of RPCs for already-queued operations, thereby
  // ensuring that previous operations use previous SubTransactionMetadata. If we do not flush here,
  // already queued operations may incorrectly use this newly modified SubTransactionMetadata when
  // they are eventually sent to DocDB.
  RETURN_NOT_OK(pg_session_->FlushBufferedOperations(PgFlushDebugContext::ActivateSubTxn(id)));
  pg_txn_manager_->SetActiveSubTransactionId(id);
  return Status::OK();
}

Status PgApiImpl::RollbackToSubTransaction(SubTransactionId id) {
  const auto status = pg_txn_manager_->RollbackToSubTransaction(ClearSessionState(), id);
  VLOG_WITH_FUNC(4) << "id: " << id << ", error: " << status;
  return status;
}

double PgApiImpl::GetTransactionPriority() const {
  return pg_txn_manager_->GetTransactionPriority();
}

YbcTxnPriorityRequirement PgApiImpl::GetTransactionPriorityType() const {
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

Status PgApiImpl::GetActiveTransactions(YbcPgSessionTxnInfo* infos, size_t num_infos) {
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

bool PgApiImpl::IsDdlModeWithRegularTransactionBlock() const {
  return pg_txn_manager_->IsDdlModeWithRegularTransactionBlock();
}

Result<bool> PgApiImpl::CurrentTransactionUsesFastPath() const {
  return pg_session_->CurrentTransactionUsesFastPath();
}

void PgApiImpl::ResetCatalogReadTime() {
  pg_session_->ResetCatalogReadPoint();
}

ReadHybridTime PgApiImpl::GetCatalogReadTime() const {
  return pg_session_->catalog_read_time();
}

Result<bool> PgApiImpl::ForeignKeyReferenceExists(
    const PgObjectId& table_id, const Slice& ybctid, YbcPgTableLocalityInfo locality_info) {
  return fk_reference_cache_.IsReferenceExists(
      table_id.database_oid, LightweightTableYbctid{table_id.object_oid, ybctid}, locality_info);
}

Status PgApiImpl::AddForeignKeyReferenceIntent(
    const PgObjectId& table_id, const Slice& ybctid,
    const PgFKReferenceCache::IntentOptions& options) {
  return fk_reference_cache_.AddIntent(
      table_id.database_oid, LightweightTableYbctid{table_id.object_oid, ybctid}, options);
}

void PgApiImpl::DeleteForeignKeyReference(PgOid table_id, const Slice& ybctid) {
  fk_reference_cache_.DeleteReference(LightweightTableYbctid{table_id, ybctid});
}

void PgApiImpl::AddForeignKeyReference(PgOid table_id, const Slice& ybctid) {
  fk_reference_cache_.AddReference(LightweightTableYbctid{table_id, ybctid});
}

void PgApiImpl::NotifyDeferredTriggersProcessingStarted() {
  fk_reference_cache_.OnDeferredTriggersProcessingStarted();
}

Status PgApiImpl::AddExplicitRowLockIntent(
    const PgObjectId& table_id, const Slice& ybctid, const YbcPgExplicitRowLockParams& params,
    const YbcPgTableLocalityInfo& locality_info, YbcPgExplicitRowLockErrorInfo& error_info) {
  ExplicitRowLockErrorInfoAdapter adapter(error_info);
  return explicit_row_lock_buffer_.Add(
      {.rowmark = params.rowmark,
       .pg_wait_policy = params.pg_wait_policy,
       .docdb_wait_policy = params.docdb_wait_policy,
       .database_id = table_id.database_oid},
      LightweightTableYbctid(table_id.object_oid, ybctid), locality_info, adapter);
}

Status PgApiImpl::FlushExplicitRowLockIntents(YbcPgExplicitRowLockErrorInfo& error_info) {
  ExplicitRowLockErrorInfoAdapter adapter(error_info);
  return explicit_row_lock_buffer_.Flush(adapter);
}

// INSERT ... ON CONFLICT batching -----------------------------------------------------------------
Status PgApiImpl::AddInsertOnConflictKey(
    PgOid table_id, const Slice& ybctid, void* state, const YbcPgInsertOnConflictKeyInfo& info) {
  return pg_session_->GetInsertOnConflictBuffer(state).AddIndexKey(
      LightweightTableYbctid(table_id, ybctid), info);
}

YbcPgInsertOnConflictKeyState PgApiImpl::InsertOnConflictKeyExists(
    PgOid table_id, const Slice& ybctid, void* state) {
  return pg_session_->GetInsertOnConflictBuffer(state).IndexKeyExists(
      LightweightTableYbctid(table_id, ybctid));
}

uint64_t PgApiImpl::GetInsertOnConflictKeyCount(void* state) {
  return pg_session_->GetInsertOnConflictBuffer(state).GetNumIndexKeys();
}

Result<YbcPgInsertOnConflictKeyInfo> PgApiImpl::DeleteInsertOnConflictKey(
    PgOid table_id, const Slice& ybctid, void* state) {
  return pg_session_->GetInsertOnConflictBuffer(state).DeleteIndexKey(
      LightweightTableYbctid(table_id, ybctid));
}

Result<YbcPgInsertOnConflictKeyInfo> PgApiImpl::DeleteNextInsertOnConflictKey(void* state) {
  return pg_session_->GetInsertOnConflictBuffer(state).DeleteNextIndexKey();
}

void PgApiImpl::AddInsertOnConflictKeyIntent(PgOid table_id, const Slice& ybctid) {
  pg_session_->GetInsertOnConflictBuffer().AddIndexKeyIntent(
      LightweightTableYbctid(table_id, ybctid));
}

void PgApiImpl::ClearAllInsertOnConflictCaches() {
  pg_session_->ClearAllInsertOnConflictBuffers();
}

void PgApiImpl::ClearInsertOnConflictCache(void* state) {
  pg_session_->ClearInsertOnConflictBuffer(state);
}

//--------------------------------------------------------------------------------------------------

void PgApiImpl::SetTimeout(int timeout_ms) {
  pg_client_.SetTimeout(timeout_ms);
}

void PgApiImpl::ClearTimeout() {
  pg_client_.ClearTimeout();
}

void PgApiImpl::SetLockTimeout(int lock_timeout_ms) {
  pg_client_.SetLockTimeout(lock_timeout_ms);
}

Result<yb::tserver::PgGetLockStatusResponsePB> PgApiImpl::GetLockStatusData(
    const std::string &table_id, const std::string &transaction_id) {
  return pg_client_.GetLockStatusData(table_id, transaction_id);
}

Result<client::TabletServersInfo> PgApiImpl::ListTabletServers() {
  return pg_client_.ListLiveTabletServers(false);
}

Status PgApiImpl::GetIndexBackfillProgress(
    std::vector<PgObjectId> oids, uint64_t* num_rows_read_from_table, double* num_rows_backfilled) {
  return pg_client_.GetIndexBackfillProgress(
      oids, num_rows_read_from_table, num_rows_backfilled);
}

Status PgApiImpl::ValidatePlacements(
    const char *live_placement_info, const char *read_replica_placement_info,
    bool check_satisfiable) {
  return pg_session_->ValidatePlacements(
      live_placement_info ? std::string(live_placement_info) : std::string(),
      read_replica_placement_info ? std::string(read_replica_placement_info) : std::string(),
      check_satisfiable);
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
    const PgObjectId& table_id, const PgObjectId& index_id, int row_oid_filtering_attr,
    bool fetch_ybctid) {
  if (!pg_sys_table_prefetcher_) {
    LOG(DFATAL) << "Sys table prefetching was not started yet";
  } else {
    pg_sys_table_prefetcher_->Register(table_id, index_id, row_oid_filtering_attr, fetch_ybctid);
  }
}

Result<bool> PgApiImpl::CheckIfPitrActive() {
  return pg_client_.CheckIfPitrActive();
}

Result<bool> PgApiImpl::IsObjectPartOfXRepl(const PgObjectId& table_id) {
  return pg_client_.IsObjectPartOfXRepl(table_id);
}

Result<TableKeyRanges> PgApiImpl::GetTableKeyRanges(
    const PgObjectId& table_id, Slice lower_bound_key, Slice upper_bound_key,
    uint64_t max_num_ranges, uint64_t range_size_bytes, bool is_forward, uint32_t max_key_length) {
  // TODO(ysql_parallel_query): consider async population of range boundaries to avoid blocking
  // calling worker on waiting for range boundaries.
  return pg_client_.GetTableKeyRanges(
      table_id, lower_bound_key, upper_bound_key, max_num_ranges, range_size_bytes, is_forward,
      max_key_length);
}

void PgApiImpl::DumpSessionState(YbcPgSessionState* session_data) {
  session_data->session_id = GetSessionID();
  pg_txn_manager_->DumpSessionState(session_data);
}

void PgApiImpl::RestoreSessionState(const YbcPgSessionState& session_data) {
  DCHECK_EQ(GetSessionID(), session_data.session_id);
  pg_txn_manager_->RestoreSessionState(session_data);
}

//--------------------------------------------------------------------------------------------------

Status PgApiImpl::NewCreateReplicationSlot(
    const char* slot_name, const char* plugin_name, PgOid database_oid,
    YbcPgReplicationSlotSnapshotAction snapshot_action, YbcLsnType lsn_type,
    YbcOrderingMode ordering_mode, PgStatement** handle) {
  return AddToCurrentPgMemctx(
      std::make_unique<PgCreateReplicationSlot>(
          pg_session_, slot_name, plugin_name, database_oid, snapshot_action, lsn_type,
          ordering_mode),
      handle);
}

Result<tserver::PgCreateReplicationSlotResponsePB> PgApiImpl::ExecCreateReplicationSlot(
    PgStatement* handle) {
  return VERIFY_RESULT_REF(GetStatementAs<PgCreateReplicationSlot>(handle)).Exec();
}

Result<tserver::PgListReplicationSlotsResponsePB> PgApiImpl::ListReplicationSlots() {
  return pg_client_.ListReplicationSlots();
}

Result<tserver::PgGetReplicationSlotResponsePB> PgApiImpl::GetReplicationSlot(
    const ReplicationSlotName& slot_name) {
  return pg_client_.GetReplicationSlot(slot_name);
}

Result<cdc::InitVirtualWALForCDCResponsePB> PgApiImpl::InitVirtualWALForCDC(
    const std::string& stream_id, const std::vector<PgObjectId>& table_ids,
    const YbcReplicationSlotHashRange* slot_hash_range, uint64_t active_pid,
    const std::vector<PgOid>& publication_oids, bool pub_all_tables) {
  return pg_client_.InitVirtualWALForCDC(
      stream_id, table_ids, slot_hash_range, active_pid, publication_oids, pub_all_tables);
}

Result<cdc::GetLagMetricsResponsePB> PgApiImpl::GetLagMetrics(
    const std::string& stream_id, int64_t* lag_metric) {
  return pg_client_.GetLagMetrics(stream_id, lag_metric);
}

Result<cdc::UpdatePublicationTableListResponsePB> PgApiImpl::UpdatePublicationTableList(
    const std::string& stream_id, const std::vector<PgObjectId>& table_ids) {
  return pg_client_.UpdatePublicationTableList(stream_id, table_ids);
}

Result<cdc::DestroyVirtualWALForCDCResponsePB> PgApiImpl::DestroyVirtualWALForCDC() {
  return pg_client_.DestroyVirtualWALForCDC();
}

Result<cdc::GetConsistentChangesResponsePB> PgApiImpl::GetConsistentChangesForCDC(
    const std::string &stream_id) {
  return pg_client_.GetConsistentChangesForCDC(stream_id);
}

Result<cdc::UpdateAndPersistLSNResponsePB> PgApiImpl::UpdateAndPersistLSN(
    const std::string& stream_id, YbcPgXLogRecPtr restart_lsn, YbcPgXLogRecPtr confirmed_flush) {
  return pg_client_.UpdateAndPersistLSN(stream_id, restart_lsn, confirmed_flush);
}

Status PgApiImpl::NewDropReplicationSlot(const char* slot_name, PgStatement** handle) {
  return AddToCurrentPgMemctx(
      std::make_unique<PgDropReplicationSlot>(pg_session_, slot_name), handle);
}

Status PgApiImpl::ExecDropReplicationSlot(PgStatement* handle) {
  return VERIFY_RESULT_REF(GetStatementAs<PgDropReplicationSlot>(handle)).Exec();
}

Result<tserver::PgYCQLStatementStatsResponsePB> PgApiImpl::YCQLStatementStats() {
  return pg_client_.YCQLStatementStats();
}

Result<tserver::PgActiveSessionHistoryResponsePB> PgApiImpl::ActiveSessionHistory() {
  return pg_client_.ActiveSessionHistory();
}

Result<tserver::PgTabletsMetadataResponsePB> PgApiImpl::TabletsMetadata(bool local_only) {
  return pg_client_.TabletsMetadata(local_only);
}

Result<tserver::PgServersMetricsResponsePB> PgApiImpl::ServersMetrics() {
    return pg_client_.ServersMetrics();
}

SetupPerformOptionsAccessorTag PgApiImpl::ClearSessionState() {
  auto result = pg_session_->DropBufferedOperations();
  fk_reference_cache_.Clear();
  explicit_row_lock_buffer_.Clear();
  pg_session_->ClearAllInsertOnConflictBuffers();
  return result;
}

bool PgApiImpl::IsCronLeader() const { return tserver_shared_object_->IsCronLeader(); }

Status PgApiImpl::SetCronLastMinute(int64_t last_minute) {
  return pg_client_.SetCronLastMinute(last_minute);
}

Result<int64_t> PgApiImpl::GetCronLastMinute() { return pg_client_.GetCronLastMinute(); }

YbcReadPointHandle PgApiImpl::GetCurrentReadPoint() const {
  return pg_txn_manager_->GetCurrentReadPoint();
}

YbcReadPointHandle PgApiImpl::GetMaxReadPoint() const {
  return pg_txn_manager_->GetMaxReadPoint();
}

Status PgApiImpl::RestoreReadPoint(YbcReadPointHandle read_point) {
  RETURN_NOT_OK(FlushBufferedOperations(PgFlushDebugContext::ChangeTxnSnapshot(read_point)));
  return pg_txn_manager_->RestoreReadPoint(read_point);
}

Result<YbcReadPointHandle> PgApiImpl::RegisterSnapshotReadTime(
    uint64_t read_time, bool use_read_time) {
  return pg_txn_manager_->RegisterSnapshotReadTime(read_time, use_read_time);
}

void PgApiImpl::DdlEnableForceCatalogModification() {
  pg_txn_manager_->DdlEnableForceCatalogModification();
}

//------------------------------------------------------------------------------------------------
// Advisory Locks.
//------------------------------------------------------------------------------------------------

Status PgApiImpl::AcquireAdvisoryLock(
      const YbcAdvisoryLockId& lock_id, YbcAdvisoryLockMode mode, bool wait, bool session) {
  return pg_session_->AcquireAdvisoryLock(lock_id, mode, wait, session);
}

Status PgApiImpl::ReleaseAdvisoryLock(const YbcAdvisoryLockId& lock_id, YbcAdvisoryLockMode mode) {
  return pg_session_->ReleaseAdvisoryLock(lock_id, mode);
}

Status PgApiImpl::ReleaseAllAdvisoryLocks(uint32_t db_oid) {
  return pg_session_->ReleaseAllAdvisoryLocks(db_oid);
}

//------------------------------------------------------------------------------------------------
// Table Locks.
//------------------------------------------------------------------------------------------------

Status PgApiImpl::AcquireObjectLock(const YbcObjectLockId& lock_id, YbcObjectLockMode mode) {
  return pg_session_->AcquireObjectLock(lock_id, mode);
}

//------------------------------------------------------------------------------------------------
// Export/Import Pg Txn Snapshot.
//------------------------------------------------------------------------------------------------

Result<std::string> PgApiImpl::ExportSnapshot(
    const YbcPgTxnSnapshot& snapshot, std::optional<YbcReadPointHandle> explicit_read_time) {
  return pg_txn_manager_->ExportSnapshot(
      VERIFY_RESULT(pg_session_->FlushBufferedOperations(
          PgFlushDebugContext::ExportSnapshot(snapshot.db_id, explicit_read_time))),
      snapshot, explicit_read_time);
}

Result<YbcPgTxnSnapshot> PgApiImpl::ImportSnapshot(std::string_view snapshot_id) {
  return pg_txn_manager_->ImportSnapshot(
      VERIFY_RESULT(pg_session_->FlushBufferedOperations(
          PgFlushDebugContext::ImportSnapshot(snapshot_id))), snapshot_id);
}

bool PgApiImpl::HasExportedSnapshots() const { return pg_txn_manager_->has_exported_snapshots(); }

void PgApiImpl::ClearExportedTxnSnapshots() { pg_txn_manager_->ClearExportedTxnSnapshots(); }

Status PgApiImpl::TriggerRelcacheInitConnection(const std::string& dbname) {
  return pg_client_.TriggerRelcacheInitConnection(dbname);
}

Status PgApiImpl::Init(std::optional<uint64_t> session_id) {
  RETURN_NOT_OK(interrupter_->Start());
  RETURN_NOT_OK(clock_->Init());
  return pg_client_.Start(
    proxy_cache_.get(), &messenger_holder_.messenger->scheduler(), *tserver_shared_object_,
    session_id);
}

Result<std::unique_ptr<PgApiImpl>> PgApiImpl::Make(
      YbcPgTypeEntities type_entities, const YbcPgCallbacks& pg_callbacks,
      const YbcPgInitPostgresInfo& init_postgres_info, YbcPgAshConfig& ash_config,
      YbcPgExecStatsState& session_stats, bool is_binary_upgrade) {
    std::unique_ptr<PgApiImpl> result{new PgApiImpl(
        type_entities, pg_callbacks, init_postgres_info, ash_config, session_stats,
        is_binary_upgrade)};
    RETURN_NOT_OK(result->Init(
      init_postgres_info.parallel_leader_session_id
          ? std::optional(*init_postgres_info.parallel_leader_session_id) : std::nullopt));
    return result;
}

} // namespace yb::pggate
