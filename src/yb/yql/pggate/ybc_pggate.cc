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

#include "yb/yql/pggate/ybc_pggate.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "yb/client/session.h"
#include "yb/client/table_info.h"
#include "yb/client/tablet_server.h"

#include "yb/common/common_flags.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/pg_types.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/dockv/pg_key_decoder.h"
#include "yb/dockv/pg_row.h"
#include "yb/dockv/reader_projection.h"
#include "yb/dockv/value_type.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/strings/numbers.h"

#include "yb/gutil/walltime.h"
#include "yb/server/clockbound_clock.h"
#include "yb/server/skewed_clock.h"

#include "yb/tserver/pg_client.pb.h"
#include "yb/util/atomic.h"
#include "yb/util/curl_util.h"
#include "yb/util/flags.h"
#include "yb/util/jwt_util.h"
#include "yb/util/result.h"
#include "yb/util/signal_util.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/tcmalloc_profile.h"
#include "yb/util/thread.h"
#include "yb/util/yb_partition.h"

#include "yb/yql/pggate/pg_expr.h"
#include "yb/yql/pggate/pg_gate_fwd.h"
#include "yb/yql/pggate/pg_tabledesc.h"
#include "yb/yql/pggate/pg_tools.h"
#include "yb/yql/pggate/pg_value.h"
#include "yb/yql/pggate/pggate.h"
#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pggate/pggate_thread_local_vars.h"
#include "yb/yql/pggate/util/pg_wire.h"
#include "yb/yql/pggate/util/ybc-internal.h"
#include "yb/yql/pggate/util/ybc_util.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

DEFINE_UNKNOWN_int32(pggate_num_connections_to_server, 1,
             "Number of underlying connections to each server from a PostgreSQL backend process. "
             "This overrides the value of --num_connections_to_server.");

DECLARE_int32(num_connections_to_server);

DECLARE_int32(delay_alter_sequence_sec);

DECLARE_bool(ysql_enable_colocated_tables_with_tablespaces);

DECLARE_bool(TEST_ysql_enable_db_logical_client_version_mode);

DEFINE_UNKNOWN_bool(ysql_enable_reindex, false,
            "Enable REINDEX INDEX statement.");
TAG_FLAG(ysql_enable_reindex, advanced);
TAG_FLAG(ysql_enable_reindex, hidden);

DEFINE_UNKNOWN_bool(ysql_disable_server_file_access, false,
            "If true, disables read, write, and execute of local server files. "
            "File access can be re-enabled if set to false.");

DEFINE_NON_RUNTIME_bool(ysql_enable_profile, false, "Enable PROFILE feature.");

DEFINE_test_flag(string, ysql_conn_mgr_dowarmup_all_pools_mode, "none",
  "Enable precreation of server connections in every pool in Ysql Connection Manager and "
  "choose the mode of attachment of idle server connections to clients to serve their queries. "
  "ysql_conn_mgr_dowarmup is responsible for creating server connections only in "
  "yugabyte (user), yugabyte (database) pool during the initialization of connection "
  "manager process. This flag will create max(ysql_conn_mgr_min_conns_per_db, "
  "3) number of server connections in any pool whenever there is a requirement to create the "
  "first backend process in that particular pool.");

DEFINE_test_flag(uint32, ysql_conn_mgr_auth_delay_ms, 0,
    "Add a delay in od_auth_backend to simulate stalls during authentication with connection "
    " manager .");

DEFINE_NON_RUNTIME_bool(ysql_conn_mgr_superuser_sticky, true,
  "If enabled, make superuser connections sticky in Ysql Connection Manager.");

DEFINE_NON_RUNTIME_int32(ysql_conn_mgr_max_query_size, 4096,
  "Maximum size of the query which connection manager can process in the deploy phase or while"
  "forwarding the client query");

DEFINE_NON_RUNTIME_int32(ysql_conn_mgr_wait_timeout_ms, 10000,
  "ysql_conn_mgr_wait_timeout_ms denotes the waiting time in ms, before getting timeout while "
  "sending/receiving the packets at the socket in ysql connection manager. It is seen"
  " asan builds requires large wait timeout than other builds");

// This gflag should be deprecated but kept to avoid breaking some customer
// clusters using it. Use ysql_catalog_preload_additional_table_list if possible.
DEFINE_NON_RUNTIME_bool(ysql_catalog_preload_additional_tables, false,
    "If true, YB catalog preloads a default set of tables upon connection "
    "creation and cache refresh: pg_am,pg_amproc,pg_cast,pg_cast,pg_inherits,"
    "pg_policy,pg_proc,pg_tablespace,pg_trigger.");

DEFINE_NON_RUNTIME_string(ysql_catalog_preload_additional_table_list, "",
    "A list of catalog tables that YSQL preloads additionally upon "
    "connection start-up and cache refreshes. Catalog table names must start with pg_."
    "Invalid catalog names are ignored. Comma separated. Example: pg_range,pg_proc."
    "If both ysql_catalog_preload_additional_tables and "
    "ysql_catalog_preload_additional_table_list are set, we take a union of "
    "both the default list and the user-specified list.");

DEFINE_NON_RUNTIME_bool(ysql_disable_global_impact_ddl_statements, false,
            "If true, disable global impact ddl statements in per database catalog "
            "version mode.");

DEPRECATE_FLAG(bool, ysql_disable_per_tuple_memory_context_in_update_relattrs, "06_2023");

DEFINE_NON_RUNTIME_bool(
    ysql_minimal_catalog_caches_preload, false,
    "Fill postgres' caches with system items only");

DEFINE_RUNTIME_PREVIEW_bool(
  ysql_conn_mgr_version_matching, false,
  "If true, does selection of transactional backends based on logical client version");

DEFINE_RUNTIME_PREVIEW_bool(
    ysql_conn_mgr_version_matching_connect_higher_version, true,
    "If ysql_conn_mgr_version_matching is enabled is enabled, then connect to higher version "
    "server if this flag is set to true");

DEFINE_NON_RUNTIME_bool(ysql_block_dangerous_roles, false,
    "Block roles that can potentially be used to escalate to superuser privileges. Intended to be "
    "used with superuser login disabled, such as in YBM. When true, this assumes those blocked "
    "roles are not already in use.");

DEFINE_RUNTIME_PREVIEW_bool(
    ysql_enable_pg_export_snapshot, false,
    "Enables the support for synchronizing snapshots across transactions, using pg_export_snapshot "
    "and SET TRANSACTION SNAPSHOT");

DEFINE_NON_RUNTIME_bool(ysql_enable_neghit_full_inheritscache, true,
    "When set to true, a (fully) preloaded inherits cache returns negative cache hits"
    " right away without incurring a master lookup");

DEFINE_RUNTIME_PG_FLAG(
    bool, yb_force_early_ddl_serialization, false,
    "If object locking is off (i.e., TEST_enable_object_locking_for_table_locks=false), concurrent "
    "DDLs might face a conflict error on the catalog version increment at the end after doing all "
    "the work. Setting this flag enables a fail-fast strategy by locking the catalog version at "
    "the start of DDLs, causing conflict errors to occur before useful work is done. This flag is "
    "only applicable without object locking. If object locking is enabled, it ensures that "
    "concurrent DDLs block on each other for serialization. Also, this flag is valid only if "
    "ysql_enable_db_catalog_version_mode and yb_enable_invalidation_messages are enabled.");

DEFINE_NON_RUNTIME_bool(ysql_enable_read_request_cache_for_connection_auth, false,
            "If true, use tserver response cache for authorization processing "
            "during connection setup. Only applicable when connection manager "
            "is used.");

DEFINE_NON_RUNTIME_bool(ysql_enable_scram_channel_binding, false,
    "Offer the option of SCRAM-SHA-256-PLUS (i.e. SCRAM with channel binding) as an SASL method if "
    "the server supports it in the SASL-Authentication message. This flag is disabled by default "
    "as connection manager does not support SCRAM with channel binding and enabling it would "
    "cause different behaviour vis-a-vis direct connections to postgres.");

DECLARE_bool(TEST_ash_debug_aux);
DECLARE_bool(TEST_generate_ybrowid_sequentially);
DECLARE_bool(TEST_ysql_log_perdb_allocated_new_objectid);

DECLARE_bool(use_fast_backward_scan);
DECLARE_uint32(ysql_max_invalidation_message_queue_size);
DECLARE_uint32(max_replication_slots);
DECLARE_bool(ysql_yb_enable_implicit_dynamic_tables_logical_replication);
DECLARE_int32(timestamp_history_retention_interval_sec);

/* Constants for replication slot LSN types */
const std::string YBC_LSN_TYPE_SEQUENCE = "SEQUENCE";
const std::string YBC_LSN_TYPE_HYBRID_TIME = "HYBRID_TIME";

namespace {

bool PreloadAdditionalCatalogListValidator(const char* flag_name, const std::string& flag_val) {
  for (const char& c : flag_val) {
    if (c != '_' && c != ',' && !islower(c)) {
      LOG_FLAG_VALIDATION_ERROR(flag_name, flag_val) << "Found invalid character '" << c << "'";
      return false;
    }
  }

  return true;
}

} // namespace

DEFINE_validator(ysql_catalog_preload_additional_table_list, PreloadAdditionalCatalogListValidator);

YbcRecordTempRelationDDL_hook_type YBCRecordTempRelationDDL_hook =
    &YBCDdlEnableForceCatalogModification;

namespace yb::pggate {

//--------------------------------------------------------------------------------------------------
// C++ Implementation.
// All C++ objects and structures in this module are listed in the following namespace.
//--------------------------------------------------------------------------------------------------
namespace {

inline YbcStatus YBCStatusOK() {
  return nullptr;
}

class ThreadIdChecker {
 public:
  bool operator()() const {
    return std::this_thread::get_id() == thread_id_;
  }

 private:
  const std::thread::id thread_id_ = std::this_thread::get_id();
};

// Using a raw pointer here to fully control object initialization and destruction.
pggate::PgApiImpl* pgapi;
std::atomic<bool> pgapi_shutdown_done;
const ThreadIdChecker is_main_thread;

template<class T, class Functor>
YbcStatus ExtractValueFromResult(Result<T> result, const Functor& functor) {
  if (result.ok()) {
    functor(std::move(*result));
    return YBCStatusOK();
  }
  return ToYBCStatus(result.status());
}

template<class T>
YbcStatus ExtractValueFromResult(Result<T> result, T* value) {
  return ExtractValueFromResult(std::move(result), [value](T src) {
    *value = std::move(src);
  });
}

template<class Processor>
Status ProcessYbctidImpl(const YbcPgYBTupleIdDescriptor& source, const Processor& processor) {
  auto ybctid = VERIFY_RESULT(pgapi->BuildTupleId(source));
  return processor(source.table_relfilenode_oid, ybctid.AsSlice());
}

template<class Processor>
YbcStatus ProcessYbctid(const YbcPgYBTupleIdDescriptor& source, const Processor& processor) {
  return ToYBCStatus(ProcessYbctidImpl(source, processor));
}

Slice YbctidAsSlice(uint64_t ybctid) {
  return YbctidAsSlice(pgapi->pg_types(), ybctid);
}

inline std::optional<Bound> MakeBound(YbcPgBoundType type, uint16_t value) {
  if (type == YB_YQL_BOUND_INVALID) {
    return std::nullopt;
  }
  return Bound{.value = value, .is_inclusive = (type == YB_YQL_BOUND_VALID_INCLUSIVE)};
}

Status InitPgGateImpl(
    YbcPgTypeEntities type_entities, const YbcPgCallbacks& pg_callbacks,
    const YbcPgAshConfig& ash_config, const YbcPgInitPostgresInfo& init_postgres_info) {
  // TODO: We should get rid of hybrid clock usage in YSQL backend processes (see #16034).
  // However, this is added to allow simulating and testing of some known bugs until we remove
  // HybridClock usage.
  server::SkewedClock::Register();
  server::RegisterClockboundClockProvider();

  InitThreading();

  CHECK(!pgapi) << ": " << __PRETTY_FUNCTION__ << " can only be called once";

  YBCInitFlags();

#ifndef NDEBUG
  HybridTime::TEST_SetPrettyToString(true);
#endif

  pgapi_shutdown_done.exchange(false);

  pgapi = new PgApiImpl(type_entities, pg_callbacks, init_postgres_info, ash_config);
  RETURN_NOT_OK(pgapi->StartPgApi(
      init_postgres_info.parallel_leader_session_id
          ? std::optional(*init_postgres_info.parallel_leader_session_id) : std::nullopt));

  VLOG(1) << "PgGate open";
  return Status::OK();
}

Status PgInitSessionImpl(YbcPgExecStatsState& session_stats, bool is_binary_upgrade) {
  return WithMaskedYsqlSignals([&session_stats, is_binary_upgrade] {
    pgapi->InitSession(session_stats, is_binary_upgrade);
    return static_cast<Status>(Status::OK());
  });
}

// ql_value is modified in-place.
dockv::PgValue DecodeCollationEncodedString(dockv::PgTableRow* row, Slice value, size_t idx) {
  auto body = pggate::DecodeCollationEncodedString(value);
  return row->TrimString(idx, body.cdata() - value.cdata(), body.size());
}

Status GetSplitPoints(YbcPgTableDesc table_desc,
                      const YbcPgTypeEntity **type_entities,
                      YbcPgTypeAttrs *type_attrs_arr,
                      YbcPgSplitDatum *split_datums,
                      bool *has_null, bool *has_gin_null) {
  CHECK(table_desc->IsRangePartitioned());
  const Schema& schema = table_desc->schema();
  size_t num_range_key_columns = table_desc->num_range_key_columns();
  size_t num_splits = table_desc->GetPartitionListSize() - 1;
  dockv::ReaderProjection projection(schema, schema.key_column_ids());
  dockv::PgTableRow table_row(projection);

  // decode DocKeys
  const auto& partitions_bounds = table_desc->GetPartitionList();
  for (size_t split_idx = 0; split_idx < num_splits; ++split_idx) {
    // +1 skip the first empty string lower bound partition key
    Slice column_bounds(partitions_bounds[split_idx + 1]);

    for (size_t col_idx = 0; col_idx < num_range_key_columns; ++col_idx) {
      size_t split_datum_idx = split_idx * num_range_key_columns + col_idx;
      SCHECK(!column_bounds.empty(), Corruption, "Incomplete column bounds");
      auto entry_type = static_cast<dockv::KeyEntryType>(column_bounds[0]);
      if (entry_type == dockv::KeyEntryType::kLowest) {
        column_bounds.consume_byte();
        // deal with boundary cases: MINVALUE and MAXVALUE
        split_datums[split_datum_idx].datum_kind = YB_YQL_DATUM_LIMIT_MIN;
      } else if (entry_type == dockv::KeyEntryType::kHighest) {
        column_bounds.consume_byte();
        split_datums[split_datum_idx].datum_kind = YB_YQL_DATUM_LIMIT_MAX;
      } else if (entry_type == dockv::KeyEntryType::kGinNull) {
        *has_gin_null = true;
        return Status::OK();
      } else {
        table_row.Reset();
        RETURN_NOT_OK(dockv::PgKeyDecoder::DecodeEntry(
            &column_bounds, schema.column(col_idx), &table_row, col_idx));
        split_datums[split_datum_idx].datum_kind = YB_YQL_DATUM_STANDARD_VALUE;

        auto val = table_row.GetValueByIndex(col_idx);
        const bool is_null = !val;
        if (!is_null) {
          // Decode Collation
          if (entry_type == dockv::KeyEntryType::kCollString ||
              entry_type == dockv::KeyEntryType::kCollStringDescending) {
            *val = DecodeCollationEncodedString(&table_row, val->string_value(), col_idx);
          }

          RETURN_NOT_OK(PgValueToDatum(
              type_entities[col_idx], type_attrs_arr[col_idx], *val,
              &split_datums[split_datum_idx].datum));
        } else {
          *has_null = true;
          return Status::OK();
        }
      }
    }
  }

  return Status::OK();
}

void YBCStartSysTablePrefetchingImpl(std::optional<PrefetcherOptions::CachingInfo> caching_info) {
  pgapi->StartSysTablePrefetching({caching_info, implicit_cast<uint64_t>(yb_fetch_row_limit)});
}

PrefetchingCacheMode YBCMapPrefetcherCacheMode(YbcPgSysTablePrefetcherCacheMode mode) {
  switch (mode) {
    case YB_YQL_PREFETCHER_TRUST_CACHE_AUTH:
      return PrefetchingCacheMode::TRUST_CACHE_AUTH;
    case YB_YQL_PREFETCHER_TRUST_CACHE:
      return PrefetchingCacheMode::TRUST_CACHE;
    case YB_YQL_PREFETCHER_RENEW_CACHE_SOFT:
      return PrefetchingCacheMode::RENEW_CACHE_SOFT;
    case YB_YQL_PREFETCHER_RENEW_CACHE_HARD:
      LOG(ERROR) << "Emergency fallback prefetching cache mode is used";
      return PrefetchingCacheMode::RENEW_CACHE_HARD;
  }
  LOG(DFATAL) << "Unexpected YbcPgSysTablePrefetcherCacheMode value " << mode;
  return PrefetchingCacheMode::RENEW_CACHE_HARD;
}

// Sets the client address in the ASH circular buffer.
void AshGetTServerClientAddress(
    uint8_t addr_family, const std::string& host, unsigned char* client_addr) {
  // If addr_family is AF_UNIX or AF_UNSPEC, it will be nulled out in the view.
  switch (addr_family) {
    case AF_UNIX:
    case AF_UNSPEC:
      break;
    case AF_INET:
    case AF_INET6: {
      int res = inet_pton(addr_family, host.c_str(), client_addr);
      if (res == 0) {
        LOG(DFATAL) << "IP address not in presentation format";
      } else if (res == -1) {
        LOG(DFATAL) << "Not a valid address family: " << addr_family;
      }
      break;
    }
    default:
      LOG(DFATAL) << "Unknown address family found: " << addr_family;
  }
}

void AshCopyAuxInfo(
    const WaitStateInfoPB& tserver_sample, uint32_t component, YbcAshSample* cb_sample) {
  // Copy the entire aux info, or the first 15 bytes, whichever is smaller.
  // This expects compilation with -Wno-format-truncation.
  const auto& tserver_aux_info = tserver_sample.aux_info();
  snprintf(
      cb_sample->aux_info, sizeof(cb_sample->aux_info), "%s",
      FLAGS_TEST_ash_debug_aux ? tserver_aux_info.method().c_str()
                               : (component == std::to_underlying(ash::Component::kYCQL)
                                      ? tserver_aux_info.table_id().c_str()
                                      : tserver_aux_info.tablet_id().c_str()));
}

void AshCopyTServerSample(
    YbcAshSample* cb_sample, uint32_t component, const WaitStateInfoPB& tserver_sample,
    uint64_t sample_time, float sample_weight) {
  auto* cb_metadata = &cb_sample->metadata;
  const auto& tserver_metadata = tserver_sample.metadata();

  cb_metadata->query_id = tserver_metadata.query_id();
  // if the pid is zero, it's a tserver background activity
  cb_metadata->pid = tserver_metadata.pid() ? tserver_metadata.pid()
                                            : pgapi->GetLocalTServerPid();
  cb_metadata->database_id = tserver_metadata.database_id();
  cb_sample->rpc_request_id = tserver_metadata.rpc_request_id();
  cb_sample->encoded_wait_event_code =
      ash::WaitStateInfo::AshEncodeWaitStateCodeWithComponent(
        component, tserver_sample.wait_state_code());
  cb_sample->sample_weight = sample_weight;
  cb_sample->sample_time = sample_time;

  std::memcpy(cb_metadata->root_request_id,
              tserver_metadata.root_request_id().data(),
              sizeof(cb_metadata->root_request_id));

  std::memcpy(cb_sample->top_level_node_id,
              tserver_metadata.top_level_node_id().data(),
              sizeof(cb_sample->top_level_node_id));

  AshCopyAuxInfo(tserver_sample, component, cb_sample);

  cb_metadata->addr_family = tserver_metadata.addr_family();
  AshGetTServerClientAddress(cb_metadata->addr_family, tserver_metadata.client_host_port().host(),
                             cb_metadata->client_addr);
  cb_metadata->client_port =  static_cast<uint16_t>(tserver_metadata.client_host_port().port());
}

void AshCopyTServerSamples(
    YbcAshGetNextCircularBufferSlot get_cb_slot_fn, const tserver::WaitStatesPB& samples,
    uint64_t sample_time) {
  for (const auto& sample : samples.wait_states()) {
    AshCopyTServerSample(get_cb_slot_fn(), samples.component(), sample, sample_time,
        samples.sample_weight());
  }
}

std::string HumanReadableTableType(yb::TableType table_type) {
  switch (table_type) {
    case yb::TableType::PGSQL_TABLE_TYPE: return "YSQL";
    case yb::TableType::YQL_TABLE_TYPE: return "YCQL";
    case yb::TableType::TRANSACTION_STATUS_TABLE_TYPE: return "System";
    case yb::TableType::REDIS_TABLE_TYPE: return "Unknown";
  }
  FATAL_INVALID_ENUM_VALUE(yb::TableType, table_type);
}

const YbcPgTypeEntity* GetTypeEntity(
    YbcPgOid pg_type_oid, int attr_num, YbcPgOid table_oid,
    YbcTypeEntityProvider type_entity_provider) {

  // TODO(23239): Optimize the lookup of type entities for dynamic types.
  return pg_type_oid == kPgInvalidOid ? (*type_entity_provider)(attr_num, table_oid)
                                      : pgapi->pg_types().Find(pg_type_oid);
}

Status YBCGetTableKeyRangesImpl(
    const PgObjectId& table_id, Slice lower_bound_key, Slice upper_bound_key,
    uint64_t max_num_ranges, uint64_t range_size_bytes, bool is_forward, uint32_t max_key_length,
    YbcGetTableKeyRangesCallback callback, void* callback_param) {
  auto encoded_table_range_slices = VERIFY_RESULT(pgapi->GetTableKeyRanges(
      table_id, lower_bound_key, upper_bound_key, max_num_ranges, range_size_bytes, is_forward,
      max_key_length));
  for (size_t i = 0; i < encoded_table_range_slices.size(); ++i) {
    auto encoded_tablet_ranges = encoded_table_range_slices[i].AsSlice();
    while (!encoded_tablet_ranges.empty()) {
      // Consume null-flag
      RETURN_NOT_OK(encoded_tablet_ranges.consume_byte(0));
      // Read key from buffer.
      const auto key_size = PgWire::ReadNumber<uint64_t>(&encoded_tablet_ranges);
      Slice key(encoded_tablet_ranges.cdata(), key_size);
      encoded_tablet_ranges.remove_prefix(key_size);

      callback(callback_param, key.cdata(), key.size());
    }
  }

  return Status::OK();
}

static Result<std::string> GetYbLsnTypeString(
    const tserver::PGReplicationSlotLsnType yb_lsn_type, const std::string& stream_id) {
  switch (yb_lsn_type) {
    case tserver::PGReplicationSlotLsnType::ReplicationSlotLsnTypePg_SEQUENCE:
      return YBC_LSN_TYPE_SEQUENCE;
    case tserver::PGReplicationSlotLsnType::ReplicationSlotLsnTypePg_HYBRID_TIME:
      return YBC_LSN_TYPE_HYBRID_TIME;
    default:
      LOG(ERROR) << "Received unexpected LSN type " << yb_lsn_type << " for stream " << stream_id;
      return STATUS_FORMAT(
          InternalError, "Received unexpected LSN type $0 for stream $1", yb_lsn_type, stream_id);
  }
}

inline YbcPgExplicitRowLockStatus MakePgExplicitRowLockStatus() {
  return {
      .ybc_status = YBCStatusOK(),
      .error_info = {.is_initialized = false,
                     .pg_wait_policy = 0,
                     .conflicting_table_id = kInvalidOid}};
}

// YugabyteDB-specific binary upgrade flag
static bool yb_is_binary_upgrade = false;

Status YBCInitTransactionImpl(const YbcPgInitTransactionData& data) {
  RETURN_NOT_OK(pgapi->BeginTransaction(data.xact_start_timestamp));
  RETURN_NOT_OK(pgapi->SetTransactionIsolationLevel(data.effective_pggate_isolation_level));
  RETURN_NOT_OK(pgapi->UpdateFollowerReadsConfig(
      data.read_from_followers_enabled, data.follower_read_staleness_ms));
  RETURN_NOT_OK(pgapi->SetTransactionReadOnly(data.xact_read_only));
  RETURN_NOT_OK(pgapi->SetEnableTracing(data.enable_tracing));
  return pgapi->SetTransactionDeferrable(data.xact_deferrable);
}

Status YBCCommitTransactionIntermediateImpl(const YbcPgInitTransactionData& data) {
  const auto history_cutoff_guard = pgapi->TemporaryDisableReadTimeHistoryCutoff();
  RETURN_NOT_OK(pgapi->CommitPlainTransaction());
  return YBCInitTransactionImpl(data);
}

} // namespace

//--------------------------------------------------------------------------------------------------
// C API.
//--------------------------------------------------------------------------------------------------

extern "C" {

YbcStatus YBCInitPgGate(
    YbcPgTypeEntities type_entities, const YbcPgCallbacks *pg_callbacks,
    const YbcPgInitPostgresInfo *init_postgres_info, const YbcPgAshConfig *ash_config) {
  return ToYBCStatus(WithMaskedYsqlSignals(
      [&type_entities, pg_callbacks, init_postgres_info, ash_config] {
        return InitPgGateImpl(type_entities, *pg_callbacks, *ash_config, *init_postgres_info);
      }));
}

void YBCDestroyPgGate() {
  LOG_IF(FATAL, !is_main_thread())
      << __PRETTY_FUNCTION__ << " should only be invoked from the main thread";

  if (pgapi_shutdown_done.exchange(true)) {
    LOG(DFATAL) << __PRETTY_FUNCTION__ << " should only be called once";
    return;
  }
  {
    std::unique_ptr<pggate::PgApiImpl> local_pgapi(pgapi);
    pgapi = nullptr; // YBCPgIsYugaByteEnabled() must return false from now on.
  }
  VLOG(1) << __PRETTY_FUNCTION__ << " finished";
}

void YBCInterruptPgGate() {
  LOG_IF(FATAL, !is_main_thread())
      << __PRETTY_FUNCTION__ << " should only be invoked from the main thread";

  pgapi->Interrupt();
}

const YbcPgCallbacks *YBCGetPgCallbacks() {
  return pgapi->pg_callbacks();
}

YbcStatus YBCValidateJWT(const char *token, const YbcPgJwtAuthOptions *options) {
  const std::string token_value(DCHECK_NOTNULL(token));
  std::vector<std::string> identity_claims;

  auto status = util::ValidateJWT(token_value, *options, &identity_claims);
  if (!status.ok()) {
    return ToYBCStatus(status);
  }

  // There must be at least one identity claim to match to.
  // In the case of claim keys such as "sub" or "email", there will be exactly one entry while in
  // the case of "groups"/"roles", there can be more than one.
  // As long as there is a match with a single value of the list, the JWT is considered to be issued
  // for a valid username.
  int match_result = YBC_STATUS_ERROR;
  for (const auto& identity : identity_claims) {
    VLOG(4) << "Identity claim entry for JWT authentication: " << identity;
    match_result = YBCGetPgCallbacks()->CheckUserMap(
        options->usermap, options->username, identity.c_str(), false);
    if (match_result == YBC_STATUS_OK) {
      VLOG(4) << "Identity match between IDP user " << identity << " and YSQL user "
              << options->username;
      break;
    }
  }

  if (match_result == YBC_STATUS_OK) {
    return YBCStatusOK();
  }
  return ToYBCStatus(STATUS(InvalidArgument, "Identity match failed"));
}

YbcStatus YBCFetchFromUrl(const char *url, char **buf) {
  const std::string url_value(DCHECK_NOTNULL(url));
  EasyCurl curl;
  faststring buf_ret;
  auto status = curl.FetchURL(url_value, &buf_ret);
  if (!status.ok()) {
    return ToYBCStatus(status);
  }

  *DCHECK_NOTNULL(buf) = static_cast<char*>(YBCPAlloc(buf_ret.size()+1));
  snprintf(*buf, buf_ret.size()+1, "%s", buf_ret.ToString().c_str());
  return YBCStatusOK();
}

void YBCDumpCurrentPgSessionState(YbcPgSessionState* session_data) {
  pgapi->DumpSessionState(session_data);
}

void YBCRestorePgSessionState(const YbcPgSessionState* session_data) {
  CHECK_NOTNULL(pgapi);
  pgapi->RestoreSessionState(*session_data);
}

YbcStatus YBCPgInitSession(YbcPgExecStatsState* session_stats, bool is_binary_upgrade) {
  return ToYBCStatus(PgInitSessionImpl(*session_stats, is_binary_upgrade));
}

void YBCPgIncrementIndexRecheckCount() {
  pgapi->IncrementIndexRecheckCount();
}

uint64_t YBCPgGetSessionID() { return pgapi->GetSessionID(); }

YbcPgMemctx YBCPgCreateMemctx() {
  return pgapi->CreateMemctx();
}

YbcStatus YBCPgDestroyMemctx(YbcPgMemctx memctx) {
  return ToYBCStatus(pgapi->DestroyMemctx(memctx));
}

void YBCPgResetCatalogReadTime() {
  return pgapi->ResetCatalogReadTime();
}

YbcStatus YBCPgResetMemctx(YbcPgMemctx memctx) {
  return ToYBCStatus(pgapi->ResetMemctx(memctx));
}

void YBCPgDeleteStatement(YbcPgStatement handle) {
  pgapi->DeleteStatement(handle);
}

YbcStatus YBCPgInvalidateCache(uint64_t min_ysql_catalog_version) {
  return ToYBCStatus(pgapi->InvalidateCache(min_ysql_catalog_version));
}

const YbcPgTypeEntity *YBCPgFindTypeEntity(YbcPgOid type_oid) {
  return pgapi->pg_types().Find(type_oid);
}

int64_t YBCGetPgggateCurrentAllocatedBytes() {
  return GetTCMallocCurrentAllocatedBytes();
}

int64_t YBCGetActualHeapSizeBytes() {
  return pgapi ? pgapi->GetRootMemTracker().consumption() : 0;
}

bool YBCTryMemConsume(int64_t bytes) {
  if (pgapi) {
    pgapi->GetMemTracker().Consume(bytes);
    return true;
  }
  return false;
}

bool YBCTryMemRelease(int64_t bytes) {
  if (pgapi) {
    pgapi->GetMemTracker().Release(bytes);
    return true;
  }
  return false;
}

YbcStatus YBCGetHeapConsumption(YbcTcmallocStats *desc) {
  memset(desc, 0x0, sizeof(YbcTcmallocStats));
#if YB_TCMALLOC_ENABLED
  desc->total_physical_bytes = GetTCMallocPhysicalBytesUsed();

  // This excludes unmapped pages for both Google TCMalloc and GPerfTools TCMalloc.
  desc->heap_size_bytes = GetTCMallocCurrentHeapSizeBytes();

  desc->current_allocated_bytes = GetTCMallocCurrentAllocatedBytes();
  desc->pageheap_free_bytes = GetTCMallocPageHeapFreeBytes();
  desc->pageheap_unmapped_bytes = GetTCMallocPageHeapUnmappedBytes();
#endif
  return YBCStatusOK();
}

int64_t YBCGetTCMallocSamplingPeriod() { return GetTCMallocSamplingPeriod(); }

void YBCSetTCMallocSamplingPeriod(int64_t sample_period_bytes) {
  SetTCMallocSamplingPeriod(sample_period_bytes);
}

YbcStatus YBCGetHeapSnapshot(
    YbcHeapSnapshotSample** snapshot, int64_t* num_samples, bool peak_heap) {
  // Always sort by estimated bytes on Google TCMalloc (sampled bytes if gperftools).
  // If the user wants to sort by another field instead,
  // they can always use an ORDER BY clause in the query.
  Result<std::vector<Sample>> heap_snapshot_result = GetAggregateAndSortHeapSnapshot(
      GetTCMallocDefaultSampleOrder(),
      peak_heap ? HeapSnapshotType::kPeakHeap : HeapSnapshotType::kCurrentHeap,
      SampleFilter::kAllSamples);
  if (!heap_snapshot_result.ok()) {
    return ToYBCStatus(heap_snapshot_result.status());
  }
  const std::vector<Sample>& samples = heap_snapshot_result.get();
  *num_samples = samples.size();
  *snapshot = static_cast<YbcHeapSnapshotSample*>(
      YBCPAlloc(sizeof(YbcHeapSnapshotSample) * samples.size()));
  for (size_t i = 0; i < samples.size(); ++i) {
    const auto& sample = samples[i];
    auto& snapshot_sample = (*snapshot)[i];

    snapshot_sample.estimated_bytes = sample.second.estimated_bytes.value_or(0);
    snapshot_sample.estimated_bytes_is_null = !sample.second.estimated_bytes.has_value();

    snapshot_sample.estimated_count = sample.second.estimated_count.value_or(0);
    snapshot_sample.estimated_count_is_null = !sample.second.estimated_count.has_value();

    if (sample.second.estimated_bytes && sample.second.estimated_count) {
      snapshot_sample.avg_bytes_per_allocation =
          sample.second.estimated_bytes.value() /
          std::max(sample.second.estimated_count.value(), int64_t(1));
      snapshot_sample.avg_bytes_per_allocation_is_null = false;
    } else {
      snapshot_sample.avg_bytes_per_allocation = 0;
      snapshot_sample.avg_bytes_per_allocation_is_null = true;
    }

    snapshot_sample.sampled_bytes = sample.second.sampled_allocated_bytes;
    snapshot_sample.sampled_bytes_is_null = false;

    snapshot_sample.sampled_count = sample.second.sampled_count;
    snapshot_sample.sampled_count_is_null = false;

    snapshot_sample.call_stack = sample.first.empty() ? nullptr : strdup(sample.first.c_str());
    snapshot_sample.call_stack_is_null = sample.first.empty();
  }
  return YBCStatusOK();
}

static std::string PrintOptionalInt(std::optional<int64> value) {
  if (value) return SimpleItoaWithCommas(*value);
  return "N/A";
}

void YBCDumpTcMallocHeapProfile(bool peak_heap, size_t max_call_stacks) {
  auto sample_result = GetAggregateAndSortHeapSnapshot(
      GetTCMallocDefaultSampleOrder(),
      peak_heap ? HeapSnapshotType::kPeakHeap : HeapSnapshotType::kCurrentHeap,
      SampleFilter::kAllSamples);
  if (!sample_result.ok()) {
    LOG(ERROR) << "Failed to get heap snapshot: " << sample_result.status().message();
    return;
  }
  std::vector<Sample> samples = sample_result.get();

  LOG(INFO) << "Heap Profile: ";
  for (size_t i = 0; i < std::min(samples.size(), max_call_stacks); ++i) {
    const auto& entry = samples.at(i);

    LOG(INFO) << "estimated bytes: " << PrintOptionalInt(entry.second.estimated_bytes)
              << ", estimated count: " << PrintOptionalInt(entry.second.estimated_count)
              << ", sampled_allocated bytes: "
              << PrintOptionalInt(entry.second.sampled_allocated_bytes)
              << ", sampled count: " << PrintOptionalInt(entry.second.sampled_count)
              << ", call stack: \n"
              << entry.first << "================";
  }
}

//--------------------------------------------------------------------------------------------------
// YB Bitmap Scan Operations
//--------------------------------------------------------------------------------------------------

using UnorderedSliceSet = std::unordered_set<Slice, Slice::Hash>;

static void FreeSlice(Slice slice) {
  delete[] slice.data(), slice.size();
}

YbcSliceSet YBCBitmapCreateSet() {
  UnorderedSliceSet *set = new UnorderedSliceSet();
  return set;
}

size_t YBCBitmapUnionSet(YbcSliceSet sa, YbcConstSliceSet sb) {
  UnorderedSliceSet *a = reinterpret_cast<UnorderedSliceSet *>(sa);
  const UnorderedSliceSet *b = reinterpret_cast<const UnorderedSliceSet *>(sb);
  size_t new_bytes = 0;

  // std::set_union would be appropriate here (and likely a more efficient implementation), but it's
  // important that we do not leak the allocations that we do not include in the result set.

  for (auto slice : *b) {
    if (a->insert(slice).second == false)
      FreeSlice(slice);
    else
      new_bytes += slice.size() + sizeof(slice);
  }
  delete b;

  return new_bytes;
}

YbcSliceSet YBCBitmapIntersectSet(YbcSliceSet sa, YbcSliceSet sb) {
  UnorderedSliceSet *a = reinterpret_cast<UnorderedSliceSet *>(sa);
  UnorderedSliceSet *b = reinterpret_cast<UnorderedSliceSet *>(sb);

  // we want to iterate over the smaller set to save some time
  if (b->size() < a->size()) {
    std::swap(a, b);
  }

  // for each elem in a, if it's not also in b, delete a's copy
  auto iterb = b->begin();
  for (auto itera = a->begin(); itera != a->end();) {
    if ((iterb = b->find(*itera)) == b->end()) {
      // We cannot modify the slice while it is in the set because
      // std::unordered_set stores const Slices. Since the slice contains
      // malloc'd memory, we grab a reference to it to delete the memory after
      // removing it from the set.
      auto data = itera->data();
      itera = a->erase(itera);
      delete[] data;
    } else {
      ++itera;
    }
  }

  // then delete everything from b (a copy already exists in a)
  YBCBitmapDeepDeleteSet(b);

  return a;
}

size_t YBCBitmapInsertYbctidsIntoSet(YbcSliceSet set, YbcConstSliceVector vec) {
  const std::vector<Slice> *v = reinterpret_cast<const std::vector<Slice> *>(vec);

  size_t bytes = 0;
  UnorderedSliceSet *s = reinterpret_cast<UnorderedSliceSet *>(set);

  for (auto ybctid : *v) {
    if (s->insert(ybctid).second) // successfully inserted
      bytes += ybctid.size() + sizeof(ybctid);
    else
      FreeSlice(ybctid);
  }

  return bytes;
}

YbcConstSliceVector YBCBitmapCopySetToVector(YbcConstSliceSet set, size_t *size) {
  const UnorderedSliceSet *s = reinterpret_cast<const UnorderedSliceSet *>(set);
  if (size)
    *size = s->size();
  return new std::vector<Slice>(s->begin(), s->end());
}

YbcConstSliceVector YBCBitmapGetVectorRange(YbcConstSliceVector vec, size_t start, size_t length) {
  const std::vector<Slice> *v = reinterpret_cast<const std::vector<Slice> *>(vec);

  const size_t end_index = std::min(start + length, v->size());

  if (end_index <= start)
    return nullptr;

  return new std::vector<Slice>(v->begin() + start, v->begin() + end_index);
}

void YBCBitmapShallowDeleteVector(YbcConstSliceVector vec) {
  delete reinterpret_cast<const std::vector<Slice> *>(vec);
}

void YBCBitmapShallowDeleteSet(YbcConstSliceVector set) {
  delete reinterpret_cast<const UnorderedSliceSet *>(set);
}

void YBCBitmapDeepDeleteSet(YbcConstSliceSet set) {
  const UnorderedSliceSet *s = reinterpret_cast<const UnorderedSliceSet *>(set);
  for (auto &slice : *s)
    FreeSlice(slice);
  delete s;
}

size_t YBCBitmapGetSetSize(YbcConstSliceSet set) {
  if (!set)
    return 0;
  const UnorderedSliceSet *s = reinterpret_cast<const UnorderedSliceSet *>(set);
  return s->size();
}

size_t YBCBitmapGetVectorSize(YbcConstSliceVector vec) {
  if (!vec)
    return 0;
  const std::vector<Slice> *v = reinterpret_cast<const std::vector<Slice> *>(vec);
  return v->size();
}

//--------------------------------------------------------------------------------------------------
// DDL Statements.
//--------------------------------------------------------------------------------------------------
// Database Operations -----------------------------------------------------------------------------

YbcStatus YBCPgIsDatabaseColocated(const YbcPgOid database_oid, bool *colocated,
                                   bool *legacy_colocated_database) {
  return ToYBCStatus(pgapi->IsDatabaseColocated(database_oid, colocated,
                                                legacy_colocated_database));
}

YbcStatus YBCPgNewCreateDatabase(
    const char* database_name, const YbcPgOid database_oid, const YbcPgOid source_database_oid,
    const YbcPgOid next_oid, const bool colocated, YbcCloneInfo *yb_clone_info,
    YbcPgStatement* handle) {
  return ToYBCStatus(pgapi->NewCreateDatabase(
      database_name, database_oid, source_database_oid, next_oid, colocated,
      yb_clone_info, handle));
}

YbcStatus YBCPgExecCreateDatabase(YbcPgStatement handle) {
  return ToYBCStatus(pgapi->ExecCreateDatabase(handle));
}

YbcStatus YBCPgNewDropDatabase(const char *database_name,
                               const YbcPgOid database_oid,
                               YbcPgStatement *handle) {
  return ToYBCStatus(pgapi->NewDropDatabase(database_name, database_oid, handle));
}

YbcStatus YBCPgExecDropDatabase(YbcPgStatement handle) {
  return ToYBCStatus(pgapi->ExecDropDatabase(handle));
}

YbcStatus YBCPgNewAlterDatabase(const char *database_name,
                               const YbcPgOid database_oid,
                               YbcPgStatement *handle) {
  return ToYBCStatus(pgapi->NewAlterDatabase(database_name, database_oid, handle));
}

YbcStatus YBCPgAlterDatabaseRenameDatabase(YbcPgStatement handle, const char *newname) {
  return ToYBCStatus(pgapi->AlterDatabaseRenameDatabase(handle, newname));
}

YbcStatus YBCPgExecAlterDatabase(YbcPgStatement handle) {
  return ToYBCStatus(pgapi->ExecAlterDatabase(handle));
}

YbcStatus YBCPgReserveOids(const YbcPgOid database_oid,
                           const YbcPgOid next_oid,
                           const uint32_t count,
                           YbcPgOid *begin_oid,
                           YbcPgOid *end_oid) {
  return ToYBCStatus(pgapi->ReserveOids(database_oid, next_oid,
                                        count, begin_oid, end_oid));
}

YbcStatus YBCGetNewObjectId(YbcPgOid db_oid, YbcPgOid* new_oid) {
  DCHECK_NE(db_oid, kInvalidOid);
  return ToYBCStatus(pgapi->GetNewObjectId(db_oid, new_oid));
}

YbcStatus YBCPgGetCatalogMasterVersion(uint64_t *version) {
  return ToYBCStatus(pgapi->GetCatalogMasterVersion(version));
}

YbcStatus YBCPgInvalidateTableCacheByTableId(const char *table_id) {
  if (table_id == nullptr) {
    return ToYBCStatus(STATUS(InvalidArgument, "table_id is null"));
  }
  std::string table_id_str = table_id;
  const PgObjectId pg_object_id(table_id_str);
  pgapi->InvalidateTableCache(pg_object_id);
  return YBCStatusOK();
}

void YBCPgAlterTableInvalidateTableByOid(
    const YbcPgOid database_oid, const YbcPgOid table_relfilenode_oid) {
  pgapi->InvalidateTableCache(PgObjectId(database_oid, table_relfilenode_oid));
}

// Tablegroup Operations ---------------------------------------------------------------------------

YbcStatus YBCPgNewCreateTablegroup(const char *database_name,
                                   YbcPgOid database_oid,
                                   YbcPgOid tablegroup_oid,
                                   YbcPgOid tablespace_oid,
                                   YbcPgStatement *handle) {
  return ToYBCStatus(pgapi->NewCreateTablegroup(database_name,
                                                database_oid,
                                                tablegroup_oid,
                                                tablespace_oid,
                                                handle));
}

YbcStatus YBCPgExecCreateTablegroup(YbcPgStatement handle) {
  return ToYBCStatus(pgapi->ExecCreateTablegroup(handle));
}

YbcStatus YBCPgNewDropTablegroup(YbcPgOid database_oid,
                                 YbcPgOid tablegroup_oid,
                                 YbcPgStatement *handle) {
  return ToYBCStatus(pgapi->NewDropTablegroup(database_oid,
                                              tablegroup_oid,
                                              handle));
}
YbcStatus YBCPgExecDropTablegroup(YbcPgStatement handle) {
  return ToYBCStatus(pgapi->ExecDropTablegroup(handle));
}

// Sequence Operations -----------------------------------------------------------------------------

YbcStatus YBCInsertSequenceTuple(int64_t db_oid,
                                 int64_t seq_oid,
                                 uint64_t ysql_catalog_version,
                                 bool is_db_catalog_version_mode,
                                 int64_t last_val,
                                 bool is_called) {
  return ToYBCStatus(pgapi->InsertSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, is_db_catalog_version_mode, last_val, is_called));
}

YbcStatus YBCUpdateSequenceTupleConditionally(int64_t db_oid,
                                              int64_t seq_oid,
                                              uint64_t ysql_catalog_version,
                                              bool is_db_catalog_version_mode,
                                              int64_t last_val,
                                              bool is_called,
                                              int64_t expected_last_val,
                                              bool expected_is_called,
                                              bool *skipped) {
  return ToYBCStatus(
      pgapi->UpdateSequenceTupleConditionally(
          db_oid, seq_oid, ysql_catalog_version, is_db_catalog_version_mode,
          last_val, is_called, expected_last_val, expected_is_called, skipped));
}

YbcStatus YBCUpdateSequenceTuple(int64_t db_oid,
                                 int64_t seq_oid,
                                 uint64_t ysql_catalog_version,
                                 bool is_db_catalog_version_mode,
                                 int64_t last_val,
                                 bool is_called,
                                 bool* skipped) {
  return ToYBCStatus(pgapi->UpdateSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, is_db_catalog_version_mode,
      last_val, is_called, skipped));
}

YbcStatus YBCFetchSequenceTuple(int64_t db_oid,
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
  return ToYBCStatus(pgapi->FetchSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, is_db_catalog_version_mode, fetch_count, inc_by,
      min_value, max_value, cycle, first_value, last_value));
}

YbcStatus YBCReadSequenceTuple(int64_t db_oid,
                               int64_t seq_oid,
                               uint64_t ysql_catalog_version,
                               bool is_db_catalog_version_mode,
                               int64_t *last_val,
                               bool *is_called) {
  return ToYBCStatus(pgapi->ReadSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, is_db_catalog_version_mode, last_val, is_called));
}

YbcStatus YBCPgNewDropSequence(const YbcPgOid database_oid,
                               const YbcPgOid sequence_oid,
                               YbcPgStatement *handle) {
  return ToYBCStatus(pgapi->NewDropSequence(database_oid, sequence_oid, handle));
}

YbcStatus YBCPgExecDropSequence(YbcPgStatement handle) {
  return ToYBCStatus(pgapi->ExecDropSequence(handle));
}

YbcStatus YBCPgNewDropDBSequences(const YbcPgOid database_oid,
                                  YbcPgStatement *handle) {
  return ToYBCStatus(pgapi->NewDropDBSequences(database_oid, handle));
}

// Table Operations -------------------------------------------------------------------------------

YbcStatus YBCPgNewCreateTable(const char *database_name,
                              const char *schema_name,
                              const char *table_name,
                              YbcPgOid database_oid,
                              YbcPgOid table_relfilenode_oid,
                              bool is_shared_table,
                              bool is_sys_catalog_table,
                              bool if_not_exist,
                              YbcPgYbrowidMode ybrowid_mode,
                              bool is_colocated_via_database,
                              YbcPgOid tablegroup_oid,
                              YbcPgOid colocation_id,
                              YbcPgOid tablespace_oid,
                              bool is_matview,
                              YbcPgOid pg_table_oid,
                              YbcPgOid old_relfilenode_oid,
                              bool is_truncate,
                              YbcPgStatement *handle) {
  const PgObjectId table_id(database_oid, table_relfilenode_oid);
  const PgObjectId tablegroup_id(database_oid, tablegroup_oid);
  const PgObjectId tablespace_id(database_oid, tablespace_oid);
  const PgObjectId pg_table_id(database_oid, pg_table_oid);
  const PgObjectId old_relfilenode_id(database_oid, old_relfilenode_oid);

  return ToYBCStatus(pgapi->NewCreateTable(
      database_name, schema_name, table_name, table_id, is_shared_table, is_sys_catalog_table,
      if_not_exist, ybrowid_mode, is_colocated_via_database, tablegroup_id, colocation_id,
      tablespace_id, is_matview, pg_table_id, old_relfilenode_id, is_truncate, handle));
}

YbcStatus YBCPgCreateTableAddColumn(YbcPgStatement handle, const char *attr_name, int attr_num,
                                    const YbcPgTypeEntity *attr_type, bool is_hash, bool is_range,
                                    bool is_desc, bool is_nulls_first) {
  return ToYBCStatus(pgapi->CreateTableAddColumn(handle, attr_name, attr_num, attr_type,
                                                 is_hash, is_range, is_desc, is_nulls_first));
}

YbcStatus YBCPgCreateTableSetNumTablets(YbcPgStatement handle, int32_t num_tablets) {
  return ToYBCStatus(pgapi->CreateTableSetNumTablets(handle, num_tablets));
}

YbcStatus YBCPgAddSplitBoundary(YbcPgStatement handle, YbcPgExpr *exprs, int expr_count) {
  return ToYBCStatus(pgapi->AddSplitBoundary(handle, exprs, expr_count));
}

YbcStatus YBCPgExecCreateTable(YbcPgStatement handle) {
  return ToYBCStatus(pgapi->ExecCreateTable(handle));
}

YbcStatus YBCPgNewAlterTable(const YbcPgOid database_oid,
                             const YbcPgOid table_relfilenode_oid,
                             YbcPgStatement *handle) {
  const PgObjectId table_id(database_oid, table_relfilenode_oid);
  return ToYBCStatus(pgapi->NewAlterTable(table_id, handle));
}

YbcStatus YBCPgAlterTableAddColumn(YbcPgStatement handle, const char *name, int order,
                                   const YbcPgTypeEntity *attr_type, YbcPgExpr missing_value) {
  return ToYBCStatus(pgapi->AlterTableAddColumn(handle, name, order, attr_type, missing_value));
}

YbcStatus YBCPgAlterTableRenameColumn(YbcPgStatement handle, const char *oldname,
                                      const char *newname) {
  return ToYBCStatus(pgapi->AlterTableRenameColumn(handle, oldname, newname));
}

YbcStatus YBCPgAlterTableDropColumn(YbcPgStatement handle, const char *name) {
  return ToYBCStatus(pgapi->AlterTableDropColumn(handle, name));
}

YbcStatus YBCPgAlterTableSetReplicaIdentity(YbcPgStatement handle, const char identity_type) {
  return ToYBCStatus(pgapi->AlterTableSetReplicaIdentity(handle, identity_type));
}

YbcStatus YBCPgAlterTableRenameTable(YbcPgStatement handle, const char *db_name,
                                     const char *newname) {
  return ToYBCStatus(pgapi->AlterTableRenameTable(handle, db_name, newname));
}

YbcStatus YBCPgAlterTableIncrementSchemaVersion(YbcPgStatement handle) {
  return ToYBCStatus(pgapi->AlterTableIncrementSchemaVersion(handle));
}

YbcStatus YBCPgAlterTableSetTableId(
    YbcPgStatement handle, const YbcPgOid database_oid, const YbcPgOid table_relfilenode_oid) {
  const PgObjectId table_id(database_oid, table_relfilenode_oid);
  return ToYBCStatus(pgapi->AlterTableSetTableId(handle, table_id));
}

YbcStatus YBCPgAlterTableSetSchema(YbcPgStatement handle, const char *schema_name) {
  return ToYBCStatus(pgapi->AlterTableSetSchema(handle, schema_name));
}

YbcStatus YBCPgExecAlterTable(YbcPgStatement handle) {
  return ToYBCStatus(pgapi->ExecAlterTable(handle));
}

YbcStatus YBCPgAlterTableInvalidateTableCacheEntry(YbcPgStatement handle) {
  return ToYBCStatus(pgapi->AlterTableInvalidateTableCacheEntry(handle));
}

YbcStatus YBCPgNewDropTable(const YbcPgOid database_oid,
                            const YbcPgOid table_relfilenode_oid,
                            bool if_exist,
                            YbcPgStatement *handle) {
  const PgObjectId table_id(database_oid, table_relfilenode_oid);
  return ToYBCStatus(pgapi->NewDropTable(table_id, if_exist, handle));
}

YbcStatus YBCPgGetTableDesc(const YbcPgOid database_oid,
                            const YbcPgOid table_relfilenode_oid,
                            YbcPgTableDesc *handle) {
  const PgObjectId table_id(database_oid, table_relfilenode_oid);
  return ToYBCStatus(pgapi->GetTableDesc(table_id, handle));
}

YbcStatus YBCPgGetColumnInfo(YbcPgTableDesc table_desc,
                             int16_t attr_number,
                             YbcPgColumnInfo *column_info) {
  return ExtractValueFromResult(pgapi->GetColumnInfo(table_desc, attr_number), column_info);
}

YbcStatus YBCPgSetCatalogCacheVersion(YbcPgStatement handle, uint64_t version) {
  return ToYBCStatus(pgapi->SetCatalogCacheVersion(handle, version));
}

YbcStatus YBCPgSetDBCatalogCacheVersion(
    YbcPgStatement handle, YbcPgOid db_oid, uint64_t version) {
  return ToYBCStatus(pgapi->SetCatalogCacheVersion(handle, version, db_oid));
}

YbcStatus YBCPgSetTablespaceOid(YbcPgStatement handle, uint32_t tablespace_oid) {
  return ToYBCStatus(pgapi->SetTablespaceOid(handle, tablespace_oid));
}

#ifndef NDEBUG
void YBCPgCheckTablespaceOid(uint32_t db_oid, uint32_t table_oid, uint32_t tablespace_oid) {
  pgapi->CheckTablespaceOid(db_oid, table_oid, tablespace_oid);
}
#endif

YbcStatus YBCPgDmlModifiesRow(YbcPgStatement handle, bool *modifies_row) {
  return ExtractValueFromResult(pgapi->DmlModifiesRow(handle), modifies_row);
}

YbcStatus YBCPgSetIsSysCatalogVersionChange(YbcPgStatement handle) {
  return ToYBCStatus(pgapi->SetIsSysCatalogVersionChange(handle));
}

YbcStatus YBCPgNewTruncateTable(const YbcPgOid database_oid,
                                const YbcPgOid table_relfilenode_oid,
                                YbcPgStatement *handle) {
  const PgObjectId table_id(database_oid, table_relfilenode_oid);
  return ToYBCStatus(pgapi->NewTruncateTable(table_id, handle));
}

YbcStatus YBCPgExecTruncateTable(YbcPgStatement handle) {
  return ToYBCStatus(pgapi->ExecTruncateTable(handle));
}

YbcStatus YBCPgGetTableProperties(YbcPgTableDesc table_desc,
                                  YbcTableProperties properties) {
  CHECK_NOTNULL(properties)->num_tablets = table_desc->GetPartitionListSize();
  properties->num_hash_key_columns = table_desc->num_hash_key_columns();
  properties->is_colocated = table_desc->IsColocated();
  properties->tablegroup_oid = table_desc->GetTablegroupOid();
  properties->colocation_id = table_desc->GetColocationId();
  properties->num_range_key_columns = table_desc->num_range_key_columns();
  return YBCStatusOK();
}

// table_desc is expected to be a PgTableDesc of a range-partitioned table.
// split_datums are expected to have an allocated size:
// num_splits * num_range_key_columns, and it is used to
// store each split point value.
YbcStatus YBCGetSplitPoints(YbcPgTableDesc table_desc,
                            const YbcPgTypeEntity **type_entities,
                            YbcPgTypeAttrs *type_attrs_arr,
                            YbcPgSplitDatum *split_datums,
                            bool *has_null, bool *has_gin_null) {
  return ToYBCStatus(GetSplitPoints(table_desc, type_entities, type_attrs_arr, split_datums,
                                    has_null, has_gin_null));
}

YbcStatus YBCPgTableExists(const YbcPgOid database_oid,
                           const YbcPgOid table_relfilenode_oid,
                           bool *exists) {
  const PgObjectId table_id(database_oid, table_relfilenode_oid);
  const auto result = pgapi->LoadTable(table_id);

  if (result.ok()) {
    *exists = true;
    return YBCStatusOK();
  } else if (result.status().IsNotFound()) {
    *exists = false;
    return YBCStatusOK();
  } else {
    return ToYBCStatus(result.status());
  }
}

YbcStatus YBCPgGetTableDiskSize(YbcPgOid table_relfilenode_oid,
                                YbcPgOid database_oid,
                                int64_t *size,
                                int32_t *num_missing_tablets) {
  return ExtractValueFromResult(pgapi->GetTableDiskSize({database_oid, table_relfilenode_oid}),
                                [size, num_missing_tablets](auto value) {
     *size = value.table_size;
     *num_missing_tablets = value.num_missing_tablets;
  });
}

// Index Operations -------------------------------------------------------------------------------

YbcStatus YBCPgNewCreateIndex(const char *database_name,
                              const char *schema_name,
                              const char *index_name,
                              const YbcPgOid database_oid,
                              const YbcPgOid index_oid,
                              const YbcPgOid table_relfilenode_oid,
                              bool is_shared_index,
                              bool is_sys_catalog_index,
                              bool is_unique_index,
                              const bool skip_index_backfill,
                              bool if_not_exist,
                              bool is_colocated_via_database,
                              const YbcPgOid tablegroup_oid,
                              const YbcPgOid colocation_id,
                              const YbcPgOid tablespace_oid,
                              const YbcPgOid pg_table_oid,
                              const YbcPgOid old_relfilenode_oid,
                              YbcPgStatement *handle) {
  const PgObjectId index_id(database_oid, index_oid);
  const PgObjectId table_id(database_oid, table_relfilenode_oid);
  const PgObjectId tablegroup_id(database_oid, tablegroup_oid);
  const PgObjectId tablespace_id(database_oid, tablespace_oid);
  const PgObjectId pg_table_id(database_oid, pg_table_oid);
  const PgObjectId old_relfilenode_id(database_oid, old_relfilenode_oid);

  return ToYBCStatus(pgapi->NewCreateIndex(database_name, schema_name, index_name, index_id,
                                           table_id, is_shared_index, is_sys_catalog_index,
                                           is_unique_index, skip_index_backfill, if_not_exist,
                                           is_colocated_via_database, tablegroup_id,
                                           colocation_id, tablespace_id, pg_table_id,
                                           old_relfilenode_id, handle));
}

YbcStatus YBCPgCreateIndexAddColumn(YbcPgStatement handle, const char *attr_name, int attr_num,
                                    const YbcPgTypeEntity *attr_type, bool is_hash, bool is_range,
                                    bool is_desc, bool is_nulls_first) {
  return ToYBCStatus(pgapi->CreateIndexAddColumn(handle, attr_name, attr_num, attr_type,
                                                 is_hash, is_range, is_desc, is_nulls_first));
}

YbcStatus YBCPgCreateIndexSetNumTablets(YbcPgStatement handle, int32_t num_tablets) {
  return ToYBCStatus(pgapi->CreateIndexSetNumTablets(handle, num_tablets));
}

YbcStatus YBCPgCreateIndexSetVectorOptions(YbcPgStatement handle, YbcPgVectorIdxOptions *options) {
  return ToYBCStatus(pgapi->CreateIndexSetVectorOptions(handle, options));
}

YbcStatus YBCPgCreateIndexSetHnswOptions(
    YbcPgStatement handle, int m, int m0, int ef_construction) {
  return ToYBCStatus(pgapi->CreateIndexSetHnswOptions(handle, m, m0, ef_construction));
}

YbcStatus YBCPgExecCreateIndex(YbcPgStatement handle) {
  return ToYBCStatus(pgapi->ExecCreateIndex(handle));
}

YbcStatus YBCPgNewDropIndex(const YbcPgOid database_oid,
                            const YbcPgOid index_oid,
                            bool if_exist,
                            bool ddl_rollback_enabled,
                            YbcPgStatement *handle) {
  const PgObjectId index_id(database_oid, index_oid);
  return ToYBCStatus(pgapi->NewDropIndex(index_id, if_exist, ddl_rollback_enabled, handle));
}

YbcStatus YBCPgExecPostponedDdlStmt(YbcPgStatement handle) {
  return ToYBCStatus(pgapi->ExecPostponedDdlStmt(handle));
}

YbcStatus YBCPgExecDropTable(YbcPgStatement handle) {
  return ToYBCStatus(pgapi->ExecDropTable(handle));
}

YbcStatus YBCPgExecDropIndex(YbcPgStatement handle) {
  return ToYBCStatus(pgapi->ExecDropIndex(handle));
}

YbcStatus YBCPgWaitForBackendsCatalogVersion(YbcPgOid dboid, uint64_t version, pid_t pid,
                                             int* num_lagging_backends) {
  return ExtractValueFromResult(pgapi->WaitForBackendsCatalogVersion(dboid, version, pid),
                                num_lagging_backends);
}

YbcStatus YBCPgBackfillIndex(
    const YbcPgOid database_oid,
    const YbcPgOid index_oid) {
  const PgObjectId index_id(database_oid, index_oid);
  return ToYBCStatus(pgapi->BackfillIndex(index_id));
}

YbcStatus YBCPgWaitVectorIndexReady(
    const YbcPgOid database_oid,
    const YbcPgOid index_oid) {
  const PgObjectId index_id(database_oid, index_oid);
  return ToYBCStatus(pgapi->WaitVectorIndexReady(index_id));
}

//--------------------------------------------------------------------------------------------------
// DML Statements.
//--------------------------------------------------------------------------------------------------

YbcStatus YBCPgDmlAppendTarget(YbcPgStatement handle, YbcPgExpr target) {
  return ToYBCStatus(pgapi->DmlAppendTarget(handle, target));
}

YbcStatus YbPgDmlAppendQual(YbcPgStatement handle, YbcPgExpr qual, bool is_for_secondary_index) {
  return ToYBCStatus(pgapi->DmlAppendQual(handle, qual, is_for_secondary_index));
}

YbcStatus YbPgDmlAppendColumnRef(
    YbcPgStatement handle, YbcPgExpr colref, bool is_for_secondary_index) {
  return ToYBCStatus(pgapi->DmlAppendColumnRef(
      handle, down_cast<PgColumnRef*>(colref), is_for_secondary_index));
}

YbcStatus YBCPgDmlBindColumn(YbcPgStatement handle, int attr_num, YbcPgExpr attr_value) {
  return ToYBCStatus(pgapi->DmlBindColumn(handle, attr_num, attr_value));
}

YbcStatus YBCPgDmlBindRow(
    YbcPgStatement handle, uint64_t ybctid, YbcBindColumn* columns, int count) {
  return ToYBCStatus(pgapi->DmlBindRow(handle, ybctid, columns, count));
}

YbcStatus YBCPgDmlAddRowUpperBound(YbcPgStatement handle,
                                    int n_col_values,
                                    YbcPgExpr *col_values,
                                    bool is_inclusive) {
    return ToYBCStatus(pgapi->DmlAddRowUpperBound(handle,
                                                    n_col_values,
                                                    col_values,
                                                    is_inclusive));
}

YbcStatus YBCPgDmlAddRowLowerBound(YbcPgStatement handle,
                                    int n_col_values,
                                    YbcPgExpr *col_values,
                                    bool is_inclusive) {
    return ToYBCStatus(pgapi->DmlAddRowLowerBound(handle,
                                                    n_col_values,
                                                    col_values,
                                                    is_inclusive));
}

YbcStatus YBCPgDmlBindColumnCondBetween(YbcPgStatement handle,
                                        int attr_num,
                                        YbcPgExpr attr_value,
                                        bool start_inclusive,
                                        YbcPgExpr attr_value_end,
                                        bool end_inclusive) {
  return ToYBCStatus(pgapi->DmlBindColumnCondBetween(handle,
                                                     attr_num,
                                                     attr_value,
                                                     start_inclusive,
                                                     attr_value_end,
                                                     end_inclusive));
}

YbcStatus YBCPgDmlBindColumnCondIsNotNull(YbcPgStatement handle, int attr_num) {
  return ToYBCStatus(pgapi->DmlBindColumnCondIsNotNull(handle, attr_num));
}

YbcStatus YBCPgDmlBindColumnCondIn(YbcPgStatement handle, YbcPgExpr lhs, int n_attr_values,
                                   YbcPgExpr *attr_values) {
  return ToYBCStatus(pgapi->DmlBindColumnCondIn(handle, lhs, n_attr_values, attr_values));
}

YbcStatus YBCPgDmlBindHashCodes(
  YbcPgStatement handle,
  YbcPgBoundType start_type, uint16_t start_value,
  YbcPgBoundType end_type, uint16_t end_value) {
  const auto start = MakeBound(start_type, start_value);
  const auto end = MakeBound(end_type, end_value);
  DCHECK(start || end);
  return ToYBCStatus(pgapi->DmlBindHashCode(handle, start, end));
}

YbcStatus YBCPgDmlBindBounds(
    YbcPgStatement handle, uint64_t lower_bound_ybctid, bool lower_bound_inclusive,
    uint64_t upper_bound_ybctid, bool upper_bound_inclusive) {
  return ToYBCStatus(pgapi->DmlBindBounds(
      handle, lower_bound_ybctid ? YbctidAsSlice(lower_bound_ybctid) : Slice(),
      lower_bound_inclusive, upper_bound_ybctid ? YbctidAsSlice(upper_bound_ybctid) : Slice(),
      upper_bound_inclusive));
}

YbcStatus YBCPgDmlBindRange(YbcPgStatement handle,
                            const char *lower_bound, size_t lower_bound_len,
                            const char *upper_bound, size_t upper_bound_len) {
  return ToYBCStatus(pgapi->DmlBindRange(
    handle, Slice(lower_bound, lower_bound_len), true,
            Slice(upper_bound, upper_bound_len), false));
}

YbcStatus YBCPgDmlBindTable(YbcPgStatement handle) {
  return ToYBCStatus(pgapi->DmlBindTable(handle));
}

YbcStatus YBCPgDmlGetColumnInfo(YbcPgStatement handle, int attr_num, YbcPgColumnInfo* column_info) {
  return ExtractValueFromResult(pgapi->DmlGetColumnInfo(handle, attr_num), column_info);
}

YbcStatus YBCPgDmlAssignColumn(YbcPgStatement handle,
                               int attr_num,
                               YbcPgExpr attr_value) {
  return ToYBCStatus(pgapi->DmlAssignColumn(handle, attr_num, attr_value));
}

YbcStatus YBCPgDmlANNBindVector(YbcPgStatement handle, YbcPgExpr vector) {
  return ToYBCStatus(pgapi->DmlANNBindVector(handle, vector));
}

YbcStatus YBCPgDmlANNSetPrefetchSize(YbcPgStatement handle, int prefetch_size) {
  return ToYBCStatus(pgapi->DmlANNSetPrefetchSize(handle, prefetch_size));
}

YbcStatus YBCPgDmlHnswSetReadOptions(YbcPgStatement handle, int ef_search) {
  return ToYBCStatus(pgapi->DmlHnswSetReadOptions(handle, ef_search));
}

YbcStatus YBCPgDmlFetch(YbcPgStatement handle, int32_t natts, uint64_t *values, bool *isnulls,
                        YbcPgSysColumns *syscols, bool *has_data) {
  return ToYBCStatus(pgapi->DmlFetch(handle, natts, values, isnulls, syscols, has_data));
}

YbcStatus YBCPgStartOperationsBuffering() {
  return ToYBCStatus(pgapi->StartOperationsBuffering());
}

YbcStatus YBCPgStopOperationsBuffering() {
  return ToYBCStatus(pgapi->StopOperationsBuffering());
}

void YBCPgResetOperationsBuffering() {
  pgapi->ResetOperationsBuffering();
}

YbcStatus YBCPgFlushBufferedOperations(YbcFlushDebugContext *debug_context) {
  return ToYBCStatus(pgapi->FlushBufferedOperations(*debug_context));
}

YbcStatus YBCPgAdjustOperationsBuffering(int multiple) {
  return ToYBCStatus(pgapi->AdjustOperationsBuffering(multiple));
}

YbcStatus YBCPgDmlExecWriteOp(YbcPgStatement handle, int32_t *rows_affected_count) {
  return ToYBCStatus(pgapi->DmlExecWriteOp(handle, rows_affected_count));
}

YbcStatus YBCPgBuildYBTupleId(const YbcPgYBTupleIdDescriptor *source, uint64_t *ybctid) {
  return ProcessYbctid(*source, [ybctid](const auto&, const auto& yid) {
    *ybctid = pgapi->pg_types().GetYbctid().yb_to_datum(
        yid.cdata(), yid.size(), nullptr /* type_attrs */);
    return Status::OK();
  });
}

YbcStatus YBCPgNewSample(
    const YbcPgOid database_oid, const YbcPgOid table_relfilenode_oid, bool is_region_local,
    int targrows, double rstate_w, uint64_t rand_state_s0, uint64_t rand_state_s1,
    YbcPgStatement *handle) {
  return ToYBCStatus(pgapi->NewSample(
      {database_oid, table_relfilenode_oid}, is_region_local,
      targrows, {.w = rstate_w, .s0 = rand_state_s0, .s1 = rand_state_s1}, handle));
}

YbcStatus YBCPgSampleNextBlock(YbcPgStatement handle, bool *has_more) {
  return ExtractValueFromResult(pgapi->SampleNextBlock(handle), has_more);
}

YbcStatus YBCPgExecSample(YbcPgStatement handle) {
  return ToYBCStatus(pgapi->ExecSample(handle));
}

YbcStatus YBCPgGetEstimatedRowCount(YbcPgStatement handle, double *liverows, double *deadrows) {
  return ExtractValueFromResult(
      pgapi->GetEstimatedRowCount(handle),
      [liverows, deadrows](const auto& count) {
        *liverows = count.live;
        *deadrows = count.dead;
      });
}

// INSERT Operations -------------------------------------------------------------------------------
YbcStatus YBCPgNewInsertBlock(
    YbcPgOid database_oid,
    YbcPgOid table_oid,
    bool is_region_local,
    YbcPgTransactionSetting transaction_setting,
    YbcPgStatement *handle) {
  auto result = pgapi->NewInsertBlock(
      PgObjectId(database_oid, table_oid), is_region_local, transaction_setting);
  if (result.ok()) {
    *handle = *result;
    return nullptr;
  }
  return ToYBCStatus(result.status());
}

YbcStatus YBCPgNewInsert(const YbcPgOid database_oid,
                         const YbcPgOid table_relfilenode_oid,
                         bool is_region_local,
                         YbcPgStatement *handle,
                         YbcPgTransactionSetting transaction_setting) {
  const PgObjectId table_id(database_oid, table_relfilenode_oid);
  return ToYBCStatus(pgapi->NewInsert(table_id, is_region_local, handle, transaction_setting));
}

YbcStatus YBCPgExecInsert(YbcPgStatement handle) {
  return ToYBCStatus(pgapi->ExecInsert(handle));
}

YbcStatus YBCPgInsertStmtSetUpsertMode(YbcPgStatement handle) {
  return ToYBCStatus(pgapi->InsertStmtSetUpsertMode(handle));
}

YbcStatus YBCPgInsertStmtSetWriteTime(YbcPgStatement handle, const uint64_t write_time) {
  HybridTime write_hybrid_time;
  YbcStatus status = ToYBCStatus(write_hybrid_time.FromUint64(write_time));
  if (status) {
    return status;
  } else {
    return ToYBCStatus(pgapi->InsertStmtSetWriteTime(handle, write_hybrid_time));
  }
}

YbcStatus YBCPgInsertStmtSetIsBackfill(YbcPgStatement handle, const bool is_backfill) {
  return ToYBCStatus(pgapi->InsertStmtSetIsBackfill(handle, is_backfill));
}

// UPDATE Operations -------------------------------------------------------------------------------
YbcStatus YBCPgNewUpdate(const YbcPgOid database_oid,
                         const YbcPgOid table_relfilenode_oid,
                         bool is_region_local,
                         YbcPgStatement *handle,
                         YbcPgTransactionSetting transaction_setting) {
  const PgObjectId table_id(database_oid, table_relfilenode_oid);
  return ToYBCStatus(pgapi->NewUpdate(table_id, is_region_local, handle, transaction_setting));
}

YbcStatus YBCPgExecUpdate(YbcPgStatement handle) {
  return ToYBCStatus(pgapi->ExecUpdate(handle));
}

// DELETE Operations -------------------------------------------------------------------------------
YbcStatus YBCPgNewDelete(const YbcPgOid database_oid,
                         const YbcPgOid table_relfilenode_oid,
                         bool is_region_local,
                         YbcPgStatement *handle,
                         YbcPgTransactionSetting transaction_setting) {
  const PgObjectId table_id(database_oid, table_relfilenode_oid);
  return ToYBCStatus(pgapi->NewDelete(table_id, is_region_local, handle, transaction_setting));
}

YbcStatus YBCPgExecDelete(YbcPgStatement handle) {
  return ToYBCStatus(pgapi->ExecDelete(handle));
}

YbcStatus YBCPgDeleteStmtSetIsPersistNeeded(YbcPgStatement handle, const bool is_persist_needed) {
  return ToYBCStatus(pgapi->DeleteStmtSetIsPersistNeeded(handle, is_persist_needed));
}

// Colocated TRUNCATE Operations -------------------------------------------------------------------
YbcStatus YBCPgNewTruncateColocated(const YbcPgOid database_oid,
                                    const YbcPgOid table_relfilenode_oid,
                                    bool is_region_local,
                                    YbcPgStatement *handle,
                                    YbcPgTransactionSetting transaction_setting) {
  const PgObjectId table_id(database_oid, table_relfilenode_oid);
  return ToYBCStatus(pgapi->NewTruncateColocated(
      table_id, is_region_local, handle, transaction_setting));
}

YbcStatus YBCPgExecTruncateColocated(YbcPgStatement handle) {
  return ToYBCStatus(pgapi->ExecTruncateColocated(handle));
}

// SELECT Operations -------------------------------------------------------------------------------
YbcStatus YBCPgNewSelect(
    YbcPgOid database_oid, YbcPgOid table_relfilenode_oid, const YbcPgPrepareParameters* params,
    bool is_region_local, YbcPgStatement* handle) {
  return ToYBCStatus(pgapi->NewSelect(
      PgObjectId{database_oid, table_relfilenode_oid},
      PgObjectId{database_oid, params ? params->index_relfilenode_oid : kInvalidOid},
      params, is_region_local, handle));
}

YbcStatus YBCPgSetForwardScan(YbcPgStatement handle, bool is_forward_scan) {
  return ToYBCStatus(pgapi->SetForwardScan(handle, is_forward_scan));
}

YbcStatus YBCPgSetDistinctPrefixLength(YbcPgStatement handle, int distinct_prefix_length) {
  return ToYBCStatus(pgapi->SetDistinctPrefixLength(handle, distinct_prefix_length));
}

YbcStatus YBCPgExecSelect(YbcPgStatement handle, const YbcPgExecParameters *exec_params) {
  return ToYBCStatus(pgapi->ExecSelect(handle, exec_params));
}

YbcStatus YBCPgRetrieveYbctids(YbcPgStatement handle, const YbcPgExecParameters *exec_params,
                                   int natts, YbcSliceVector *ybctids, size_t *count,
                                   bool *exceeded_work_mem) {
  return ExtractValueFromResult(
      pgapi->RetrieveYbctids(handle, exec_params, natts, ybctids, count),
      [exceeded_work_mem](bool retrieved) { *exceeded_work_mem = !retrieved; });
}

YbcStatus YBCPgFetchRequestedYbctids(YbcPgStatement handle, const YbcPgExecParameters *exec_params,
                                     YbcConstSliceVector ybctids) {
  return ToYBCStatus(pgapi->FetchRequestedYbctids(handle, exec_params, ybctids));
}

YbcStatus YBCPgBindYbctids(YbcPgStatement handle, int n, uintptr_t* ybctids) {
  return ToYBCStatus(pgapi->BindYbctids(handle, n, ybctids));
}

//------------------------------------------------------------------------------------------------
// Functions
//------------------------------------------------------------------------------------------------

YbcStatus YBCAddFunctionParam(
    YbcPgFunction handle, const char *name, const YbcPgTypeEntity *type_entity, uint64_t datum,
    bool is_null) {
  return ToYBCStatus(
      pgapi->AddFunctionParam(handle, std::string(name), type_entity, datum, is_null));
}

YbcStatus YBCAddFunctionTarget(
    YbcPgFunction handle, const char *attr_name, const YbcPgTypeEntity *type_entity,
    const YbcPgTypeAttrs type_attrs) {
  return ToYBCStatus(
      pgapi->AddFunctionTarget(handle, std::string(attr_name), type_entity, type_attrs));
}

YbcStatus YBCFinalizeFunctionTargets(YbcPgFunction handle) {
  return ToYBCStatus(pgapi->FinalizeFunctionTargets(handle));
}

YbcStatus YBCSRFGetNext(YbcPgFunction handle, uint64_t *values, bool *is_nulls, bool *has_data) {
  return ToYBCStatus(pgapi->SRFGetNext(handle, values, is_nulls, has_data));
}

//--------------------------------------------------------------------------------------------------
// Expression Operations
//--------------------------------------------------------------------------------------------------

YbcStatus YBCPgNewColumnRef(
    YbcPgStatement stmt, int attr_num, const YbcPgTypeEntity *type_entity,
    bool collate_is_valid_non_c, const YbcPgTypeAttrs *type_attrs, YbcPgExpr *expr_handle) {
  return ToYBCStatus(pgapi->NewColumnRef(
      stmt, attr_num, type_entity, collate_is_valid_non_c, type_attrs, expr_handle));
}

YbcStatus YBCPgNewConstant(
    YbcPgStatement stmt, const YbcPgTypeEntity *type_entity, bool collate_is_valid_non_c,
    const char *collation_sortkey, uint64_t datum, bool is_null, YbcPgExpr *expr_handle) {
  return ToYBCStatus(pgapi->NewConstant(
      stmt, type_entity, collate_is_valid_non_c, collation_sortkey, datum, is_null, expr_handle));
}

YbcStatus YBCPgNewConstantVirtual(
    YbcPgStatement stmt, const YbcPgTypeEntity *type_entity,
    YbcPgDatumKind datum_kind, YbcPgExpr *expr_handle) {
  return ToYBCStatus(pgapi->NewConstantVirtual(stmt, type_entity, datum_kind, expr_handle));
}

YbcStatus YBCPgNewConstantOp(
    YbcPgStatement stmt, const YbcPgTypeEntity *type_entity, bool collate_is_valid_non_c,
    const char *collation_sortkey, uint64_t datum, bool is_null, YbcPgExpr *expr_handle,
    bool is_gt) {
  return ToYBCStatus(pgapi->NewConstantOp(
      stmt, type_entity, collate_is_valid_non_c, collation_sortkey, datum, is_null, expr_handle,
      is_gt));
}

// Overwriting the expression's result with any desired values.
YbcStatus YBCPgUpdateConstInt2(YbcPgExpr expr, int16_t value, bool is_null) {
  return ToYBCStatus(pgapi->UpdateConstant(expr, value, is_null));
}

YbcStatus YBCPgUpdateConstInt4(YbcPgExpr expr, int32_t value, bool is_null) {
  return ToYBCStatus(pgapi->UpdateConstant(expr, value, is_null));
}

YbcStatus YBCPgUpdateConstInt8(YbcPgExpr expr, int64_t value, bool is_null) {
  return ToYBCStatus(pgapi->UpdateConstant(expr, value, is_null));
}

YbcStatus YBCPgUpdateConstFloat4(YbcPgExpr expr, float value, bool is_null) {
  return ToYBCStatus(pgapi->UpdateConstant(expr, value, is_null));
}

YbcStatus YBCPgUpdateConstFloat8(YbcPgExpr expr, double value, bool is_null) {
  return ToYBCStatus(pgapi->UpdateConstant(expr, value, is_null));
}

YbcStatus YBCPgUpdateConstText(YbcPgExpr expr, const char *value, bool is_null) {
  return ToYBCStatus(pgapi->UpdateConstant(expr, value, is_null));
}

YbcStatus YBCPgUpdateConstBinary(YbcPgExpr expr, const char *value,  int64_t bytes, bool is_null) {
  return ToYBCStatus(pgapi->UpdateConstant(expr, value, bytes, is_null));
}

YbcStatus YBCPgNewOperator(
    YbcPgStatement stmt, const char *opname, const YbcPgTypeEntity *type_entity,
    bool collate_is_valid_non_c, YbcPgExpr *op_handle) {
  return ToYBCStatus(pgapi->NewOperator(
      stmt, opname, type_entity, collate_is_valid_non_c, op_handle));
}

YbcStatus YBCPgOperatorAppendArg(YbcPgExpr op_handle, YbcPgExpr arg) {
  return ToYBCStatus(pgapi->OperatorAppendArg(op_handle, arg));
}

YbcStatus YBCPgNewTupleExpr(
    YbcPgStatement stmt, const YbcPgTypeEntity *tuple_type_entity,
    const YbcPgTypeAttrs *type_attrs, int num_elems,
    YbcPgExpr *elems, YbcPgExpr *expr_handle) {
  return ToYBCStatus(pgapi->NewTupleExpr(
      stmt, tuple_type_entity, type_attrs, num_elems, elems, expr_handle));
}

YbcStatus YBCGetDocDBKeySize(uint64_t data, const YbcPgTypeEntity *typeentity,
                            bool is_null, size_t *type_size) {

  if (typeentity == nullptr
      || typeentity->yb_type == YB_YQL_DATA_TYPE_UNKNOWN_DATA
      || !typeentity->allow_for_primary_key) {
    return ToYBCStatus(STATUS(NotSupported, "Feature is not supported"));
  }

  if (typeentity->datum_fixed_size > 0) {
    *type_size = typeentity->datum_fixed_size;
    return YBCStatusOK();
  }

  QLValue val;
  Status status = pggate::PgValueToPB(typeentity, data, is_null, val.mutable_value());
  if (!status.IsOk()) {
    return ToYBCStatus(status);
  }

  std::string key_buf;
  AppendToKey(val.value(), &key_buf);
  *type_size = key_buf.size();
  return YBCStatusOK();
}


YbcStatus YBCAppendDatumToKey(uint64_t data, const YbcPgTypeEntity *typeentity,
                              bool is_null, char *key_ptr,
                              size_t *bytes_written) {
  QLValue val;

  Status status = pggate::PgValueToPB(typeentity, data, is_null, val.mutable_value());
  if (!status.IsOk()) {
    return ToYBCStatus(status);
  }

  std::string key_buf;
  AppendToKey(val.value(), &key_buf);
  memcpy(key_ptr, key_buf.c_str(), key_buf.size());
  *bytes_written = key_buf.size();
  return YBCStatusOK();
}

uint16_t YBCCompoundHash(const char *key, size_t length) {
  return YBPartition::HashColumnCompoundValue(std::string_view(key, length));
}

//------------------------------------------------------------------------------------------------
// Transaction operation.
//------------------------------------------------------------------------------------------------

YbcStatus YBCPgBeginTransaction(int64_t start_time) {
  return ToYBCStatus(pgapi->BeginTransaction(start_time));
}

YbcStatus YBCPgRecreateTransaction() {
  return ToYBCStatus(pgapi->RecreateTransaction());
}

YbcStatus YBCPgRestartTransaction() {
  return ToYBCStatus(pgapi->RestartTransaction());
}

YbcStatus YBCPgResetTransactionReadPoint() {
  return ToYBCStatus(pgapi->ResetTransactionReadPoint());
}

double YBCGetTransactionPriority() {
  return pgapi->GetTransactionPriority();
}

YbcTxnPriorityRequirement YBCGetTransactionPriorityType() {
  return pgapi->GetTransactionPriorityType();
}

YbcStatus YBCPgEnsureReadPoint() {
  return ToYBCStatus(pgapi->EnsureReadPoint());
}

YbcStatus YBCPgRestartReadPoint() {
  return ToYBCStatus(pgapi->RestartReadPoint());
}

bool YBCIsRestartReadPointRequested() {
  return pgapi->IsRestartReadPointRequested();
}

YbcStatus YBCPgCommitPlainTransaction() {
  return ToYBCStatus(pgapi->CommitPlainTransaction());
}

YbcStatus YBCPgCommitPlainTransactionContainingDDL(
    YbcPgOid ddl_db_oid, bool ddl_is_silent_altering) {
  return ToYBCStatus(
      pgapi->CommitPlainTransaction(PgDdlCommitInfo{ddl_db_oid, ddl_is_silent_altering}));
}

YbcStatus YBCPgAbortPlainTransaction() {
  return ToYBCStatus(pgapi->AbortPlainTransaction());
}

YbcStatus YBCPgSetTransactionIsolationLevel(int isolation) {
  return ToYBCStatus(pgapi->SetTransactionIsolationLevel(isolation));
}

YbcStatus YBCPgSetTransactionReadOnly(bool read_only) {
  return ToYBCStatus(pgapi->SetTransactionReadOnly(read_only));
}

YbcStatus YBCPgUpdateFollowerReadsConfig(bool enable_follower_reads, int32_t staleness_ms) {
  return ToYBCStatus(pgapi->UpdateFollowerReadsConfig(enable_follower_reads, staleness_ms));
}

YbcStatus YBCPgSetEnableTracing(bool tracing) {
  return ToYBCStatus(pgapi->SetEnableTracing(tracing));
}

YbcStatus YBCPgSetTransactionDeferrable(bool deferrable) {
  return ToYBCStatus(pgapi->SetTransactionDeferrable(deferrable));
}

YbcStatus YBCPgSetInTxnBlock(bool in_txn_blk) {
  return ToYBCStatus(pgapi->SetInTxnBlock(in_txn_blk));
}

YbcStatus YBCPgSetReadOnlyStmt(bool read_only_stmt) {
  return ToYBCStatus(pgapi->SetReadOnlyStmt(read_only_stmt));
}

YbcStatus YBCPgSetDdlStateInPlainTransaction() {
  return ToYBCStatus(pgapi->SetDdlStateInPlainTransaction());
}

YbcStatus YBCPgEnterSeparateDdlTxnMode() {
  return ToYBCStatus(pgapi->EnterSeparateDdlTxnMode());
}

bool YBCPgHasWriteOperationsInDdlTxnMode() {
  return pgapi->HasWriteOperationsInDdlTxnMode();
}

YbcStatus YBCPgExitSeparateDdlTxnMode(YbcPgOid db_oid, bool is_silent_altering) {
  return ToYBCStatus(pgapi->ExitSeparateDdlTxnMode(db_oid, is_silent_altering));
}

YbcStatus YBCPgClearSeparateDdlTxnMode() {
  return ToYBCStatus(pgapi->ClearSeparateDdlTxnMode());
}

YbcStatus YBCPgSetActiveSubTransaction(uint32_t id) {
  return ToYBCStatus(pgapi->SetActiveSubTransaction(id));
}

YbcStatus YBCPgRollbackToSubTransaction(uint32_t id) {
  return ToYBCStatus(pgapi->RollbackToSubTransaction(id));
}

YbcStatus YBCPgGetSelfActiveTransaction(YbcPgUuid *txn_id, bool *is_null) {
  return ExtractValueFromResult(
      pgapi->GetActiveTransaction(), [txn_id, is_null](const Uuid &value) {
        if (value.IsNil()) {
          *is_null = true;
        } else {
          *is_null = false;
          value.ToBytes(txn_id->data);
        }
      });
}

YbcStatus YBCPgActiveTransactions(YbcPgSessionTxnInfo *infos, size_t num_infos) {
  return ToYBCStatus(pgapi->GetActiveTransactions(infos, num_infos));
}

bool YBCPgIsDdlMode() {
  return pgapi->IsDdlMode();
}

bool YBCCurrentTransactionUsesFastPath() {
  auto result = pgapi->CurrentTransactionUsesFastPath();
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }

  return result.get();
}

//------------------------------------------------------------------------------------------------
// System validation.
//------------------------------------------------------------------------------------------------
YbcStatus YBCPgValidatePlacements(
    const char *live_placement_info, const char *read_replica_placement_info,
    bool check_satisfiable) {
  return ToYBCStatus(pgapi->ValidatePlacements(
      live_placement_info, read_replica_placement_info, check_satisfiable));
}

// Referential Integrity Caching
YbcStatus YBCPgForeignKeyReferenceCacheDelete(const YbcPgYBTupleIdDescriptor *source) {
  return ProcessYbctid(*source, [](auto table_id, const auto& ybctid){
    pgapi->DeleteForeignKeyReference(table_id, ybctid);
    return Status::OK();
  });
}

void YBCPgDeleteFromForeignKeyReferenceCache(YbcPgOid table_relfilenode_oid, uint64_t ybctid) {
  pgapi->DeleteForeignKeyReference(table_relfilenode_oid, YbctidAsSlice(ybctid));
}

void YBCPgAddIntoForeignKeyReferenceCache(YbcPgOid table_relfilenode_oid, uint64_t ybctid) {
  pgapi->AddForeignKeyReference(table_relfilenode_oid, YbctidAsSlice(ybctid));
}

YbcStatus YBCForeignKeyReferenceExists(const YbcPgYBTupleIdDescriptor *source, bool* res) {
  return ProcessYbctid(*source, [res, source](auto table_id, const auto& ybctid) -> Status {
    *res = VERIFY_RESULT(pgapi->ForeignKeyReferenceExists(table_id, ybctid, source->database_oid));
    return Status::OK();
  });
}

YbcStatus YBCAddForeignKeyReferenceIntent(
    const YbcPgYBTupleIdDescriptor *source, bool is_region_local_relation,
    bool is_deferred_trigger) {
  return ProcessYbctid(
      *source,
      [is_region_local_relation, is_deferred_trigger](auto table_id, const auto& ybctid) {
        pgapi->AddForeignKeyReferenceIntent(
            table_id, ybctid,
            {.is_region_local = is_region_local_relation, .is_deferred = is_deferred_trigger});
        return Status::OK();
      });
}

void YBCNotifyDeferredTriggersProcessingStarted() {
  pgapi->NotifyDeferredTriggersProcessingStarted();
}

YbcPgExplicitRowLockStatus YBCAddExplicitRowLockIntent(
    YbcPgOid table_relfilenode_oid, uint64_t ybctid, YbcPgOid database_oid,
    const YbcPgExplicitRowLockParams *params, bool is_region_local) {
  auto result = MakePgExplicitRowLockStatus();
  result.ybc_status = ToYBCStatus(pgapi->AddExplicitRowLockIntent(
      PgObjectId(database_oid, table_relfilenode_oid), YbctidAsSlice(ybctid), *params,
      is_region_local, result.error_info));
  return result;
}

YbcPgExplicitRowLockStatus YBCFlushExplicitRowLockIntents() {
  auto result = MakePgExplicitRowLockStatus();
  result.ybc_status = ToYBCStatus(pgapi->FlushExplicitRowLockIntents(result.error_info));
  return result;
}

// INSERT ... ON CONFLICT batching -----------------------------------------------------------------
YbcStatus YBCPgAddInsertOnConflictKey(const YbcPgYBTupleIdDescriptor* tupleid, void* state,
                                      YbcPgInsertOnConflictKeyInfo* info) {
  return ProcessYbctid(*tupleid, [state, info](auto table_id, const auto& ybctid) {
    return pgapi->AddInsertOnConflictKey(
        table_id, ybctid, state, info ? *info : YbcPgInsertOnConflictKeyInfo());
  });
}

YbcStatus YBCPgInsertOnConflictKeyExists(const YbcPgYBTupleIdDescriptor* tupleid,
                                         void* state,
                                         YbcPgInsertOnConflictKeyState* res) {
  return ProcessYbctid(*tupleid, [res, state](auto table_id, const auto& ybctid) {
    *res = pgapi->InsertOnConflictKeyExists(table_id, ybctid, state);
    return Status::OK();
  });
}

YbcStatus YBCPgDeleteInsertOnConflictKey(const YbcPgYBTupleIdDescriptor* tupleid,
                                         void* state,
                                         YbcPgInsertOnConflictKeyInfo* info) {
  return ProcessYbctid(*tupleid, [state, info](auto table_id, const auto& ybctid) {
    auto result = VERIFY_RESULT(pgapi->DeleteInsertOnConflictKey(table_id, ybctid, state));
    *info = result;
    return (Status) Status::OK();
  });
}

YbcStatus YBCPgDeleteNextInsertOnConflictKey(void* state, YbcPgInsertOnConflictKeyInfo* info) {
  return ExtractValueFromResult(pgapi->DeleteNextInsertOnConflictKey(state), info);
}

YbcStatus YBCPgAddInsertOnConflictKeyIntent(const YbcPgYBTupleIdDescriptor* tupleid) {
  return ProcessYbctid(*tupleid, [](auto table_id, const auto& ybctid) {
    pgapi->AddInsertOnConflictKeyIntent(table_id, ybctid);
    return Status::OK();
  });
}

void YBCPgClearAllInsertOnConflictCaches() {
  pgapi->ClearAllInsertOnConflictCaches();
}

void YBCPgClearInsertOnConflictCache(void* state) {
  pgapi->ClearInsertOnConflictCache(state);
}

uint64_t YBCPgGetInsertOnConflictKeyCount(void* state) {
  return pgapi->GetInsertOnConflictKeyCount(state);
}

//--------------------------------------------------------------------------------------------------

void YBCInitFlags() {
  SetAtomicFlag(GetAtomicFlag(&FLAGS_pggate_num_connections_to_server),
                &FLAGS_num_connections_to_server);

  // TODO(neil) Init a gflag for "YB_PG_TRANSACTIONS_ENABLED" here also.
  // Mikhail agreed that this flag should just be initialized once at the beginning here.
  // Currently, it is initialized for every CREATE statement.
}

YbcStatus YBCPgIsInitDbDone(bool* initdb_done) {
  return ExtractValueFromResult(pgapi->IsInitDbDone(), initdb_done);
}

bool YBCGetDisableTransparentCacheRefreshRetry() {
  return pgapi->GetDisableTransparentCacheRefreshRetry();
}

YbcStatus YBCGetSharedCatalogVersion(uint64_t* catalog_version) {
  return ExtractValueFromResult(pgapi->GetSharedCatalogVersion(), catalog_version);
}

YbcStatus YBCGetSharedDBCatalogVersion(YbcPgOid db_oid, uint64_t* catalog_version) {
  return ExtractValueFromResult(pgapi->GetSharedCatalogVersion(db_oid), catalog_version);
}

YbcStatus YBCGetNumberOfDatabases(uint32_t* num_databases) {
  return ExtractValueFromResult(pgapi->GetNumberOfDatabases(), num_databases);
}

YbcStatus YBCCatalogVersionTableInPerdbMode(bool* perdb_mode) {
  return ExtractValueFromResult(pgapi->CatalogVersionTableInPerdbMode(), perdb_mode);
}

YbcStatus YBCGetTserverCatalogMessageLists(
    YbcPgOid db_oid, uint64_t ysql_catalog_version, uint32_t num_catalog_versions,
    YbcCatalogMessageLists* message_lists) {
  const auto result = pgapi->GetTserverCatalogMessageLists(
      db_oid, ysql_catalog_version, num_catalog_versions);
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }
  const auto &catalog_messages_lists = result.get();
  if (catalog_messages_lists.entries_size() == 0) {
    return YBCStatusOK();
  }
  message_lists->num_lists = catalog_messages_lists.entries_size();
  message_lists->message_lists = static_cast<YbcCatalogMessageList*>(
      YBCPAlloc(sizeof(YbcCatalogMessageList) * message_lists->num_lists));
  for (int i = 0; i < message_lists->num_lists; ++i) {
    YbcCatalogMessageList& current = message_lists->message_lists[i];
    if (catalog_messages_lists.entries(i).has_message_list()) {
      const auto& current_pb = catalog_messages_lists.entries(i).message_list();
      current.num_bytes = current_pb.size();
      current.message_list = static_cast<char*>(YBCPAlloc(current.num_bytes));
      std::memcpy(static_cast<void*>(current.message_list), current_pb.data(),
                  current_pb.size());
    } else {
      // This entire invalidation message list is a PG null. This will force full
      // catalog cache refresh.
      current.num_bytes = 0;
      current.message_list = nullptr;
    }
  }
  return YBCStatusOK();
}

YbcStatus YBCPgSetTserverCatalogMessageList(
    YbcPgOid db_oid, bool is_breaking_change, uint64_t new_catalog_version,
    const YbcCatalogMessageList *message_list) {
  const auto result = pgapi->SetTserverCatalogMessageList(db_oid, is_breaking_change,
                                                          new_catalog_version, message_list);
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }
  return YBCStatusOK();
}

uint64_t YBCGetSharedAuthKey() {
  return pgapi->GetSharedAuthKey();
}

const unsigned char* YBCGetLocalTserverUuid() {
  return pgapi->GetLocalTserverUuid();
}

const YbcPgGFlagsAccessor* YBCGetGFlags() {
  // clang-format off
  static YbcPgGFlagsAccessor accessor = {
      .log_ysql_catalog_versions                = &FLAGS_log_ysql_catalog_versions,
      .ysql_catalog_preload_additional_tables   = &FLAGS_ysql_catalog_preload_additional_tables,
      .ysql_disable_index_backfill              = &FLAGS_ysql_disable_index_backfill,
      .ysql_disable_server_file_access          = &FLAGS_ysql_disable_server_file_access,
      .ysql_enable_reindex                      = &FLAGS_ysql_enable_reindex,
      .ysql_num_databases_reserved_in_db_catalog_version_mode =
          &FLAGS_ysql_num_databases_reserved_in_db_catalog_version_mode,
      .ysql_output_buffer_size                  = &FLAGS_ysql_output_buffer_size,
      .ysql_output_flush_size                   = &FLAGS_ysql_output_flush_size,
      .ysql_sequence_cache_minval               = &FLAGS_ysql_sequence_cache_minval,
      .ysql_session_max_batch_size              = &FLAGS_ysql_session_max_batch_size,
      .ysql_sleep_before_retry_on_txn_conflict  = &FLAGS_ysql_sleep_before_retry_on_txn_conflict,
      .ysql_colocate_database_by_default        = &FLAGS_ysql_colocate_database_by_default,
      .ysql_enable_read_request_caching         = &FLAGS_ysql_enable_read_request_caching,
      .ysql_enable_profile                      = &FLAGS_ysql_enable_profile,
      .ysql_disable_global_impact_ddl_statements =
          &FLAGS_ysql_disable_global_impact_ddl_statements,
      .ysql_minimal_catalog_caches_preload      = &FLAGS_ysql_minimal_catalog_caches_preload,
      .ysql_enable_colocated_tables_with_tablespaces =
          &FLAGS_ysql_enable_colocated_tables_with_tablespaces,
      .ysql_enable_create_database_oid_collision_retry =
          &FLAGS_ysql_enable_create_database_oid_collision_retry,
      .ysql_catalog_preload_additional_table_list =
          FLAGS_ysql_catalog_preload_additional_table_list.c_str(),
      .ysql_use_relcache_file                   = &FLAGS_ysql_use_relcache_file,
      .ysql_use_optimized_relcache_update       = &FLAGS_ysql_use_optimized_relcache_update,
      .ysql_enable_pg_per_database_oid_allocator =
          &FLAGS_ysql_enable_pg_per_database_oid_allocator,
      .ysql_enable_db_catalog_version_mode =
          &FLAGS_ysql_enable_db_catalog_version_mode,
      .TEST_hide_details_for_pg_regress =
          &FLAGS_TEST_hide_details_for_pg_regress,
      .TEST_generate_ybrowid_sequentially =
          &FLAGS_TEST_generate_ybrowid_sequentially,
      .ysql_use_fast_backward_scan = &FLAGS_use_fast_backward_scan,
      .TEST_ysql_conn_mgr_dowarmup_all_pools_mode =
          FLAGS_TEST_ysql_conn_mgr_dowarmup_all_pools_mode.c_str(),
      .TEST_ysql_enable_db_logical_client_version_mode =
          &FLAGS_TEST_ysql_enable_db_logical_client_version_mode,
      .ysql_conn_mgr_superuser_sticky = &FLAGS_ysql_conn_mgr_superuser_sticky,
      .TEST_ysql_log_perdb_allocated_new_objectid =
          &FLAGS_TEST_ysql_log_perdb_allocated_new_objectid,
      .ysql_conn_mgr_version_matching = &FLAGS_ysql_conn_mgr_version_matching,
      .ysql_conn_mgr_version_matching_connect_higher_version =
          &FLAGS_ysql_conn_mgr_version_matching_connect_higher_version,
      .ysql_block_dangerous_roles = &FLAGS_ysql_block_dangerous_roles,
      .ysql_sequence_cache_method = FLAGS_ysql_sequence_cache_method.c_str(),
      .ysql_conn_mgr_sequence_support_mode = FLAGS_ysql_conn_mgr_sequence_support_mode.c_str(),
      .ysql_conn_mgr_max_query_size = &FLAGS_ysql_conn_mgr_max_query_size,
      .ysql_conn_mgr_wait_timeout_ms = &FLAGS_ysql_conn_mgr_wait_timeout_ms,
      .ysql_enable_pg_export_snapshot = &FLAGS_ysql_enable_pg_export_snapshot,
      .ysql_enable_neghit_full_inheritscache =
        &FLAGS_ysql_enable_neghit_full_inheritscache,
      .enable_object_locking_for_table_locks =
          &FLAGS_enable_object_locking_for_table_locks,
      .ysql_max_invalidation_message_queue_size =
          &FLAGS_ysql_max_invalidation_message_queue_size,
      .ysql_max_replication_slots                = &FLAGS_max_replication_slots,
      .ysql_yb_enable_implicit_dynamic_tables_logical_replication =
          &FLAGS_ysql_yb_enable_implicit_dynamic_tables_logical_replication,
      .ysql_enable_read_request_cache_for_connection_auth =
          &FLAGS_ysql_enable_read_request_cache_for_connection_auth,
      .ysql_enable_scram_channel_binding = &FLAGS_ysql_enable_scram_channel_binding,
      .TEST_ysql_conn_mgr_auth_delay_ms = &FLAGS_TEST_ysql_conn_mgr_auth_delay_ms,
      .timestamp_history_retention_interval_sec =
          &FLAGS_timestamp_history_retention_interval_sec,
  };
  // clang-format on
  return &accessor;
}

bool YBCPgIsYugaByteEnabled() {
  return pgapi;
}

void YBCSetLockTimeout(int lock_timeout_ms, void* extra) {
  if (!pgapi || lock_timeout_ms < 0) {
    return;
  }
  pgapi->SetLockTimeout(lock_timeout_ms);
}

void YBCSetTimeout(int timeout_ms) {
  if (!pgapi || timeout_ms <= 0) {
    return;
  }
  pgapi->SetTimeout(timeout_ms);
}

void YBCClearTimeout() {
  if (!pgapi) {
    return;
  }
  pgapi->ClearTimeout();
}

void YBCCheckForInterrupts() {
  LOG_IF(FATAL, !is_main_thread())
      << __PRETTY_FUNCTION__ << " should only be invoked from the main thread";

  // If we're in the midst of shutting down, do not bother checking for interrupts.
  if (!pgapi) {
    return;
  }

  pgapi->pg_callbacks()->CheckForInterrupts();
}

YbcStatus YBCNewGetLockStatusDataSRF(YbcPgFunction *handle) {
  return ToYBCStatus(pgapi->NewGetLockStatusDataSRF(handle));
}

YbcStatus YBCGetTabletServerHosts(YbcServerDescriptor **servers, size_t *count) {
  const auto result = pgapi->ListTabletServers();
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }
  const auto &servers_info = result.get().tablet_servers;
  *count = servers_info.size();
  *servers = nullptr;
  if (!servers_info.empty()) {
    *servers = static_cast<YbcServerDescriptor *>(
        YBCPAlloc(sizeof(YbcServerDescriptor) * servers_info.size()));
    YbcServerDescriptor *dest = *servers;
    for (const auto &info : servers_info) {
      new (dest) YbcServerDescriptor {
        .host = YBCPAllocStdString(info.server.hostname),
        .cloud = YBCPAllocStdString(info.cloud),
        .region = YBCPAllocStdString(info.region),
        .zone = YBCPAllocStdString(info.zone),
        .public_ip = YBCPAllocStdString(info.public_ip),
        .is_primary = info.is_primary,
        .pg_port = info.pg_port,
        .uuid = YBCPAllocStdString(info.server.uuid),
        .universe_uuid = YBCPAllocStdString(result.get().universe_uuid),
      };
      ++dest;
    }
  }
  return YBCStatusOK();
}

/*
 * Get the index backfill progress.
 *
 * Returns the number of base table tuples scanned and index tuples inserted
 * during the index backfill. If the backfill is in progress, it will report
 * only the tuples scanned and inserted so far. After the backfill is completed,
 * this function only returns the tuple scanned and inserted at the time of
 * completion of the backfill, not the actual number of tuples in the base table
 * and index currently.
 *
 * If an index is not found in the DocDB catalog, both return values are set to
 * UINT64_MAX for this index.
 *
 * If num_rows_backfilled is NULL, it will not be populated.
 */
YbcStatus YBCGetIndexBackfillProgress(YbcPgOid* index_oids, YbcPgOid* database_oids,
                                      uint64_t* num_rows_read_from_table,
                                      double* num_rows_backfilled, int num_indexes) {
  std::vector<PgObjectId> index_ids;
  for (int i = 0; i < num_indexes; ++i) {
    index_ids.emplace_back(PgObjectId(database_oids[i], index_oids[i]));
  }
  return ToYBCStatus(pgapi->GetIndexBackfillProgress(index_ids,
                                                     num_rows_read_from_table,
                                                     num_rows_backfilled));
}

//------------------------------------------------------------------------------------------------
// Thread-local variables.
//------------------------------------------------------------------------------------------------

void* YBCPgGetThreadLocalCurrentMemoryContext() {
  return PgGetThreadLocalCurrentMemoryContext();
}

void* YBCPgSetThreadLocalCurrentMemoryContext(void *memctx) {
  return PgSetThreadLocalCurrentMemoryContext(memctx);
}

void YBCPgResetCurrentMemCtxThreadLocalVars() {
  PgResetCurrentMemCtxThreadLocalVars();
}

void* YBCPgGetThreadLocalStrTokPtr() {
  return PgGetThreadLocalStrTokPtr();
}

void YBCPgSetThreadLocalStrTokPtr(char *new_pg_strtok_ptr) {
  PgSetThreadLocalStrTokPtr(new_pg_strtok_ptr);
}

int YBCPgGetThreadLocalYbExpressionVersion() {
  return PgGetThreadLocalYbExpressionVersion();
}

void YBCPgSetThreadLocalYbExpressionVersion(int yb_expr_version) {
  PgSetThreadLocalYbExpressionVersion(yb_expr_version);
}

YbcPgThreadLocalRegexpCache* YBCPgGetThreadLocalRegexpCache() {
  return PgGetThreadLocalRegexpCache();
}

YbcPgThreadLocalRegexpCache* YBCPgInitThreadLocalRegexpCache(
    size_t buffer_size, YbcPgThreadLocalRegexpCacheCleanup cleanup) {
  return PgInitThreadLocalRegexpCache(buffer_size, cleanup);
}

YbcPgThreadLocalRegexpMetadata* YBCPgGetThreadLocalRegexpMetadata() {
  return PgGetThreadLocalRegexpMetadata();
}

void* YBCPgSetThreadLocalJumpBuffer(void* new_buffer) {
  return PgSetThreadLocalJumpBuffer(new_buffer);
}

void* YBCPgGetThreadLocalJumpBuffer() {
  return PgGetThreadLocalJumpBuffer();
}

void* YBCPgSetThreadLocalErrStatus(void* new_status) {
  return PgSetThreadLocalErrStatus(new_status);
}

void* YBCPgGetThreadLocalErrStatus() {
  return PgGetThreadLocalErrStatus();
}

void YBCStartSysTablePrefetchingNoCache() {
  YBCStartSysTablePrefetchingImpl(std::nullopt);
}

void YBCStartSysTablePrefetching(
    YbcPgOid database_oid,
    YbcPgLastKnownCatalogVersionInfo version_info,
    YbcPgSysTablePrefetcherCacheMode cache_mode) {
  YBCStartSysTablePrefetchingImpl(PrefetcherOptions::CachingInfo{
      {version_info.version, version_info.is_db_catalog_version_mode},
      database_oid,
      YBCMapPrefetcherCacheMode(cache_mode)});
}

void YBCStopSysTablePrefetching() {
  pgapi->StopSysTablePrefetching();
}

void YBCPauseSysTablePrefetching() {
  pgapi->PauseSysTablePrefetching();
}

void YBCResumeSysTablePrefetching() {
  pgapi->ResumeSysTablePrefetching();
}

bool YBCIsSysTablePrefetchingStarted() {
  return pgapi->IsSysTablePrefetchingStarted();
}

void YBCRegisterSysTableForPrefetching(
    YbcPgOid database_oid, YbcPgOid table_oid, YbcPgOid index_oid, int row_oid_filtering_attr,
    bool fetch_ybctid) {
  pgapi->RegisterSysTableForPrefetching(
      PgObjectId(database_oid, table_oid), PgObjectId(database_oid, index_oid),
      row_oid_filtering_attr, fetch_ybctid);
}

YbcStatus YBCPrefetchRegisteredSysTables() {
  return ToYBCStatus(pgapi->PrefetchRegisteredSysTables());
}

YbcStatus YBCPgCheckIfPitrActive(bool* is_active) {
  auto res = pgapi->CheckIfPitrActive();
  if (res.ok()) {
    *is_active = *res;
    return YBCStatusOK();
  }
  return ToYBCStatus(res.status());
}

YbcStatus YBCIsObjectPartOfXRepl(YbcPgOid database_oid, YbcPgOid table_relfilenode_oid,
    bool* is_object_part_of_xrepl) {
  auto res = pgapi->IsObjectPartOfXRepl(PgObjectId(database_oid, table_relfilenode_oid));
  if (res.ok()) {
    *is_object_part_of_xrepl = *res;
    return YBCStatusOK();
  }
  return ToYBCStatus(res.status());
}

YbcStatus YBCPgCancelTransaction(const unsigned char* transaction_id) {
  return ToYBCStatus(pgapi->CancelTransaction(transaction_id));
}

YbcStatus YBCGetTableKeyRanges(
    YbcPgOid database_oid, YbcPgOid table_relfilenode_oid, const char* lower_bound_key,
    size_t lower_bound_key_size, const char* upper_bound_key, size_t upper_bound_key_size,
    uint64_t max_num_ranges, uint64_t range_size_bytes, bool is_forward, uint32_t max_key_length,
    YbcGetTableKeyRangesCallback callback, void* callback_param) {
  return ToYBCStatus(YBCGetTableKeyRangesImpl(
      PgObjectId(database_oid, table_relfilenode_oid), Slice(lower_bound_key, lower_bound_key_size),
      Slice(upper_bound_key, upper_bound_key_size), max_num_ranges, range_size_bytes, is_forward,
      max_key_length, callback, callback_param));
}

//--------------------------------------------------------------------------------------------------
// Replication Slots.

YbcStatus YBCPgNewCreateReplicationSlot(const char *slot_name,
                                        const char *plugin_name,
                                        YbcPgOid database_oid,
                                        YbcPgReplicationSlotSnapshotAction snapshot_action,
                                        YbcLsnType lsn_type,
                                        YbcOrderingMode ordering_mode,
                                        YbcPgStatement *handle) {
  return ToYBCStatus(pgapi->NewCreateReplicationSlot(slot_name,
                                                     plugin_name,
                                                     database_oid,
                                                     snapshot_action,
                                                     lsn_type,
                                                     ordering_mode,
                                                     handle));
}

YbcStatus YBCPgExecCreateReplicationSlot(YbcPgStatement handle,
                                         uint64_t *consistent_snapshot_time) {
  const auto result = pgapi->ExecCreateReplicationSlot(handle);
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }

  if (consistent_snapshot_time) {
    *consistent_snapshot_time = result->cdcsdk_consistent_snapshot_time();
  }
  return YBCStatusOK();
}

char GetReplicaIdentity(yb::tserver::PgReplicaIdentityPB replica_identity_pb) {
  switch (replica_identity_pb.replica_identity()) {
    case tserver::DEFAULT:
      return YBC_REPLICA_IDENTITY_DEFAULT;
    case tserver::FULL:
      return YBC_REPLICA_IDENTITY_FULL;
    case tserver::NOTHING:
      return YBC_REPLICA_IDENTITY_NOTHING;
    case tserver::CHANGE:
      return YBC_YB_REPLICA_IDENTITY_CHANGE;
    default:
      LOG(FATAL) << Format(
          "Received unexpected replica identity $0", replica_identity_pb.DebugString());
      return 'a';
  }
}

YbcStatus YBCPgListSlotEntries(YbcSlotEntryDescriptor** slot_entries, size_t* num_slot_entries) {
  const auto result = pgapi->ListSlotEntries();
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }
  VLOG(4) << "The ListSlotEntries response: " << result->DebugString();

  const auto& slot_entries_info = result.get().slot_entries();
  *DCHECK_NOTNULL(num_slot_entries) = slot_entries_info.size();
  *DCHECK_NOTNULL(slot_entries) = nullptr;

  if (slot_entries_info.empty()) {
    return YBCStatusOK();
  }

  *slot_entries = static_cast<YbcSlotEntryDescriptor*>(
      YBCPAlloc(sizeof(YbcSlotEntryDescriptor) * slot_entries_info.size()));
  YbcSlotEntryDescriptor* dest = *slot_entries;

  for (const auto& info : slot_entries_info) {
    new (dest) YbcSlotEntryDescriptor{
        .stream_id = YBCPAllocStdString(info.stream_id()),
        .confirmed_flush_lsn = info.confirmed_flush_lsn(),
        .restart_lsn = info.restart_lsn(),
        .xmin = info.xmin(),
        .record_id_commit_time_ht = info.record_id_commit_time_ht(),
        .last_pub_refresh_time = info.last_pub_refresh_time(),
        .active_pid = info.active_pid(),
    };
    ++dest;
  }

  return YBCStatusOK();
}

YbcStatus YBCPgListReplicationSlots(
    YbcReplicationSlotDescriptor **replication_slots, size_t *numreplicationslots) {
  const auto result = pgapi->ListReplicationSlots();
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }
  VLOG(4) << "The ListReplicationSlots response: " << result->DebugString();

  const auto &replication_slots_info = result.get().replication_slots();
  *DCHECK_NOTNULL(numreplicationslots) = replication_slots_info.size();
  *DCHECK_NOTNULL(replication_slots) = nullptr;
  if (!replication_slots_info.empty()) {
    *replication_slots = static_cast<YbcReplicationSlotDescriptor *>(
        YBCPAlloc(sizeof(YbcReplicationSlotDescriptor) * replication_slots_info.size()));
    YbcReplicationSlotDescriptor *dest = *replication_slots;
    for (const auto &info : replication_slots_info) {
      int replica_identities_count = info.replica_identity_map_size();
      YbcPgReplicaIdentityDescriptor* replica_identities =
          static_cast<YbcPgReplicaIdentityDescriptor*>(
              YBCPAlloc(sizeof(YbcPgReplicaIdentityDescriptor) * replica_identities_count));

      int replica_identity_idx = 0;
      for (const auto& replica_identity : info.replica_identity_map()) {
        replica_identities[replica_identity_idx].table_oid = replica_identity.first;
        replica_identities[replica_identity_idx].identity_type =
            GetReplicaIdentity(replica_identity.second);
        replica_identity_idx++;
      }

      auto lsn_type_result = GetYbLsnTypeString(info.yb_lsn_type(), info.stream_id());
      if (!lsn_type_result.ok()) {
        return ToYBCStatus(lsn_type_result.status());
      }

      new (dest) YbcReplicationSlotDescriptor{
          .slot_name = YBCPAllocStdString(info.slot_name()),
          .output_plugin = YBCPAllocStdString(info.output_plugin_name()),
          .stream_id = YBCPAllocStdString(info.stream_id()),
          .database_oid = info.database_oid(),
          .active = info.replication_slot_status() == tserver::ReplicationSlotStatus::ACTIVE,
          .confirmed_flush = info.confirmed_flush_lsn(),
          .restart_lsn = info.restart_lsn(),
          .xmin = info.xmin(),
          .record_id_commit_time_ht = info.record_id_commit_time_ht(),
          .replica_identities = replica_identities,
          .replica_identities_count = replica_identities_count,
          .last_pub_refresh_time = info.last_pub_refresh_time(),
          .yb_lsn_type = YBCPAllocStdString(lsn_type_result.get()),
          .active_pid = info.active_pid(),
          .expired = info.expired(),
      };
      ++dest;
    }
  }
  return YBCStatusOK();
}

YbcStatus YBCPgGetReplicationSlot(
    const char *slot_name, YbcReplicationSlotDescriptor **replication_slot) {
  const auto replication_slot_name = ReplicationSlotName(std::string(slot_name));
  const auto result = pgapi->GetReplicationSlot(replication_slot_name);
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }

  VLOG(4) << "The GetReplicationSlot for slot_name = " << std::string(slot_name)
          << " is: " << result->DebugString();

  const auto& slot_info = result.get().replication_slot_info();

  *replication_slot =
      static_cast<YbcReplicationSlotDescriptor*>(YBCPAlloc(sizeof(YbcReplicationSlotDescriptor)));

  int replica_identities_count = slot_info.replica_identity_map_size();
  YbcPgReplicaIdentityDescriptor* replica_identities = static_cast<YbcPgReplicaIdentityDescriptor*>(
      YBCPAlloc(sizeof(YbcPgReplicaIdentityDescriptor) * replica_identities_count));

  int replica_identity_idx = 0;
  for (const auto& replica_identity : slot_info.replica_identity_map()) {
    replica_identities[replica_identity_idx].table_oid = replica_identity.first;
    replica_identities[replica_identity_idx].identity_type =
        GetReplicaIdentity(replica_identity.second);
    replica_identity_idx++;
  }

  auto lsn_type_result = GetYbLsnTypeString(slot_info.yb_lsn_type(), slot_info.stream_id());
  if (!lsn_type_result.ok()) {
    return ToYBCStatus(lsn_type_result.status());
  }

  new (*replication_slot) YbcReplicationSlotDescriptor{
      .slot_name = YBCPAllocStdString(slot_info.slot_name()),
      .output_plugin = YBCPAllocStdString(slot_info.output_plugin_name()),
      .stream_id = YBCPAllocStdString(slot_info.stream_id()),
      .database_oid = slot_info.database_oid(),
      .active = slot_info.replication_slot_status() == tserver::ReplicationSlotStatus::ACTIVE,
      .confirmed_flush = slot_info.confirmed_flush_lsn(),
      .restart_lsn = slot_info.restart_lsn(),
      .xmin = slot_info.xmin(),
      .record_id_commit_time_ht = slot_info.record_id_commit_time_ht(),
      .replica_identities = replica_identities,
      .replica_identities_count = replica_identities_count,
      .last_pub_refresh_time = slot_info.last_pub_refresh_time(),
      .yb_lsn_type = YBCPAllocStdString(lsn_type_result.get()),
      .active_pid = slot_info.active_pid(),
      .expired = slot_info.expired(),
  };

  return YBCStatusOK();
}

YbcStatus YBCPgNewDropReplicationSlot(const char *slot_name,
                                      YbcPgStatement *handle) {
  return ToYBCStatus(pgapi->NewDropReplicationSlot(slot_name,
                                                   handle));
}

YbcStatus YBCPgExecDropReplicationSlot(YbcPgStatement handle) {
  return ToYBCStatus(pgapi->ExecDropReplicationSlot(handle));
}

YbcStatus YBCYcqlStatementStats(YbcYCQLStatementStats** stats, size_t* num_stats) {
  const auto result = pgapi->YCQLStatementStats();
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }
  const auto& statements_stat = result->statements();
  *num_stats = statements_stat.size();
  *stats = nullptr;
  if (!statements_stat.empty()) {
    *stats = static_cast<YbcYCQLStatementStats*>(
        YBCPAlloc(sizeof(YbcYCQLStatementStats) * statements_stat.size()));
    YbcYCQLStatementStats *dest = *stats;
    for (const auto &info : statements_stat) {
      new (dest) YbcYCQLStatementStats {
          .queryid = info.queryid(),
          .query = YBCPAllocStdString(info.query()),
          .is_prepared = info.is_prepared(),
          .calls = info.calls(),
          .total_time = info.total_time(),
          .min_time = info.min_time(),
          .max_time = info.max_time(),
          .mean_time = info.mean_time(),
          .stddev_time = info.stddev_time(),
          .keyspace = YBCPAllocStdString(info.keyspace()),
      };
      ++dest;
    }
  }
  return YBCStatusOK();
}

void YBCStoreTServerAshSamples(
    YbcAshAcquireBufferLock acquire_cb_lock_fn, YbcAshGetNextCircularBufferSlot get_cb_slot_fn,
    uint64_t sample_time) {
  const auto result = pgapi->ActiveSessionHistory();
  // This lock is released inside YbAshMain after copying PG samples
  acquire_cb_lock_fn(true /* exclusive */);
  if (!result.ok()) {
    // We don't return error status to avoid a restart loop of the ASH collector
    LOG(ERROR) << result.status();
  } else {
    AshCopyTServerSamples(get_cb_slot_fn, result->tserver_wait_states(), sample_time);
    AshCopyTServerSamples(get_cb_slot_fn, result->cql_wait_states(), sample_time);
  }
}

YbcStatus YBCPgInitVirtualWalForCDC(
    const char *stream_id, const YbcPgOid database_oid, YbcPgOid *relations, YbcPgOid *relfilenodes,
    size_t num_relations, const YbcReplicationSlotHashRange *slot_hash_range, uint64_t active_pid,
    YbcPgOid *publications, size_t num_publications, bool yb_is_pub_all_tables) {
  std::vector<PgObjectId> tables;
  tables.reserve(num_relations);

  for (size_t i = 0; i < num_relations; i++) {
    PgObjectId table_id(database_oid, relfilenodes[i]);
    tables.push_back(std::move(table_id));
  }

  std::vector<PgOid> publications_oid_list;
  publications_oid_list.reserve(num_publications);

  for (size_t i = 0; i < num_publications; i++) {
    publications_oid_list.push_back(std::move(publications[i]));
  }

  const auto result = pgapi->InitVirtualWALForCDC(
      std::string(stream_id), tables, slot_hash_range, active_pid, publications_oid_list,
      yb_is_pub_all_tables);
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }

  return YBCStatusOK();
}

YbcStatus YBCPgGetLagMetrics(const char* stream_id, int64_t* lag_metric) {
  const auto result = pgapi->GetLagMetrics(std::string(stream_id), lag_metric);
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }
  return YBCStatusOK();
}

YbcStatus YBCPgUpdatePublicationTableList(
    const char* stream_id, const YbcPgOid database_oid, YbcPgOid* relations, YbcPgOid* relfilenodes,
    size_t num_relations) {
  std::vector<PgObjectId> tables;
  tables.reserve(num_relations);

  for (size_t i = 0; i < num_relations; i++) {
    PgObjectId table_id(database_oid, relfilenodes[i]);
    tables.push_back(std::move(table_id));
  }

  const auto result = pgapi->UpdatePublicationTableList(std::string(stream_id), tables);
  if(!result.ok()) {
    return ToYBCStatus(result.status());
  }

  return YBCStatusOK();
}

YbcStatus YBCPgDestroyVirtualWalForCDC() {
  const auto result = pgapi->DestroyVirtualWALForCDC();
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }

  return YBCStatusOK();
}

YbcPgRowMessageAction GetRowMessageAction(yb::cdc::RowMessage row_message_pb) {
  switch (row_message_pb.op()) {
    case cdc::RowMessage_Op_BEGIN:
      return YB_PG_ROW_MESSAGE_ACTION_BEGIN;
    case cdc::RowMessage_Op_COMMIT:
      return YB_PG_ROW_MESSAGE_ACTION_COMMIT;
    case cdc::RowMessage_Op_INSERT:
      return YB_PG_ROW_MESSAGE_ACTION_INSERT;
    case cdc::RowMessage_Op_UPDATE:
      return YB_PG_ROW_MESSAGE_ACTION_UPDATE;
    case cdc::RowMessage_Op_DELETE:
      return YB_PG_ROW_MESSAGE_ACTION_DELETE;
    case cdc::RowMessage_Op_DDL:
      return YB_PG_ROW_MESSAGE_ACTION_DDL;
    default:
      LOG(FATAL) << Format("Received unexpected operation $0", row_message_pb.op());
      return YB_PG_ROW_MESSAGE_ACTION_UNKNOWN;
  }
}

YbcStatus YBCPgGetCDCConsistentChanges(
    const char* stream_id,
    YbcPgChangeRecordBatch** record_batch,
    YbcTypeEntityProvider type_entity_provider) {
  const auto result = pgapi->GetConsistentChangesForCDC(std::string(stream_id));
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }

  *DCHECK_NOTNULL(record_batch) = nullptr;
  const auto resp = result.get();
  VLOG(4) << "The GetConsistentChangesForCDC response: " << resp.DebugString();
  auto response_to_pg_conversion_start = GetCurrentTimeMicros();
  auto row_count = resp.cdc_sdk_proto_records_size();

  // Used for logging a summary of the response received from the CDC service.
  YbcPgXLogRecPtr min_resp_lsn = 0xFFFFFFFFFFFFFFFF;
  YbcPgXLogRecPtr max_resp_lsn = 0;
  uint32_t min_txn_id = 0xFFFFFFFF;
  uint32_t max_txn_id = 0;

  auto resp_rows_pb = resp.cdc_sdk_proto_records();
  auto resp_rows = static_cast<YbcPgRowMessage *>(YBCPAlloc(sizeof(YbcPgRowMessage) * row_count));
  bool needs_publication_table_list_refresh = resp.needs_publication_table_list_refresh();
  uint64_t publication_refresh_time = resp.publication_refresh_time();

  size_t row_idx = 0;
  for (const auto& row_pb : resp_rows_pb) {
    auto row_message_pb = row_pb.row_message();
    auto commit_time_ht = HybridTime::FromPB(row_message_pb.commit_time());

    static constexpr size_t OMITTED_VALUE = std::numeric_limits<size_t>::max();

    // column_name -> (index in old tuples, index in new tuples).
    // OMITTED_VALUE indicates omission.
    // For insert: all the columns will be of form (OMITTED_VALUE, <index>).
    // For delete: all the columns will be of form (<index>, OMITTED_VALUE) or
    // won't exist in the map depending on the record type (replica identity) of the stream.
    // For update: both old and new tuples will be present.
    std::unordered_map<std::string, std::pair<size_t, size_t>> col_name_idx_map;
    int new_tuple_idx = 0;
    for (const auto &new_tuple : row_message_pb.new_tuple()) {
      if (new_tuple.has_column_name()) {
        col_name_idx_map.emplace(
            new_tuple.column_name(), std::make_pair(OMITTED_VALUE, new_tuple_idx));
      }
      new_tuple_idx++;
    }
    int old_tuple_idx = 0;
    for (const auto& old_tuple : row_message_pb.old_tuple()) {
      if (old_tuple.has_column_name()) {
        auto itr = col_name_idx_map.find(old_tuple.column_name());
        if (itr != col_name_idx_map.end()) {
          itr->second.first = old_tuple_idx;
        } else {
          col_name_idx_map.emplace(
              old_tuple.column_name(), std::make_pair(old_tuple_idx, OMITTED_VALUE));
        }
      }
      old_tuple_idx++;
    }

    auto table_oid = kPgInvalidOid;
    if (row_message_pb.has_table_id()) {
      const PgObjectId table_id(row_message_pb.table_id());
      YbcPgTableDesc tableDesc = nullptr;
      auto s = pgapi->GetTableDesc(table_id, &tableDesc);
      if (!s.ok()) {
        return ToYBCStatus(s);
      }
      table_oid = tableDesc->pg_table_id().object_oid;
    }

    auto col_count = narrow_cast<int>(col_name_idx_map.size());
    YbcPgDatumMessage *cols = nullptr;
    if (col_count > 0) {
      cols = static_cast<YbcPgDatumMessage *>(YBCPAlloc(sizeof(YbcPgDatumMessage) * col_count));

      int tuple_idx = 0;
      for (const auto& col_idxs : col_name_idx_map) {
        YbcPgTypeAttrs type_attrs{-1 /* typmod */};
        const auto& column_name = col_idxs.first;

        // Before Op value aka Before Image.
        uint64 before_op_datum = 0;
        bool before_op_is_null = true;
        bool before_op_is_omitted = col_idxs.second.first == OMITTED_VALUE;
        if (!before_op_is_omitted) {
          const auto old_datum_pb =
              &row_message_pb.old_tuple(static_cast<int>(col_idxs.second.first));
          DCHECK(table_oid != kPgInvalidOid);
          const auto* type_entity = GetTypeEntity(
              narrow_cast<YbcPgOid>(old_datum_pb->column_type()), old_datum_pb->col_attr_num(),
              table_oid, type_entity_provider);
          auto s = PBToDatum(
              type_entity, type_attrs, old_datum_pb->pg_ql_value(), &before_op_datum,
              &before_op_is_null);
          if (!s.ok()) {
            return ToYBCStatus(s);
          }
        }

        // After Op value.
        uint64 after_op_datum = 0;
        bool after_op_is_null = true;
        bool after_op_is_omitted = col_idxs.second.second == OMITTED_VALUE;
        if (!after_op_is_omitted) {
          const auto new_datum_pb =
              &row_message_pb.new_tuple(static_cast<int>(col_idxs.second.second));
          DCHECK(table_oid != kPgInvalidOid);
          const auto* type_entity = GetTypeEntity(
              narrow_cast<YbcPgOid>(new_datum_pb->column_type()), new_datum_pb->col_attr_num(),
              table_oid, type_entity_provider);
          auto s = PBToDatum(
              type_entity, type_attrs, new_datum_pb->pg_ql_value(), &after_op_datum,
              &after_op_is_null);
          if (!s.ok()) {
            return ToYBCStatus(s);
          }
        }

        auto col = &cols[tuple_idx++];
        col->column_name = YBCPAllocStdString(column_name);
        col->after_op_datum = after_op_datum;
        col->after_op_is_null = after_op_is_null;
        col->after_op_is_omitted = after_op_is_omitted;
        col->before_op_datum = before_op_datum;
        col->before_op_is_null = before_op_is_null;
        col->before_op_is_omitted = before_op_is_omitted;
      }
    }


    new (&resp_rows[row_idx]) YbcPgRowMessage{
        .col_count = col_count,
        .cols = cols,
        // Convert the physical component of the HT to PG epoch.
        .commit_time = static_cast<uint64_t>(
            YBCGetPgCallbacks()->UnixEpochToPostgresEpoch(commit_time_ht.GetPhysicalValueMicros())),
        .commit_time_ht = commit_time_ht.ToUint64(),
        .action = GetRowMessageAction(row_message_pb),
        .table_oid = table_oid,
        .lsn = row_message_pb.pg_lsn(),
        .xid = row_message_pb.pg_transaction_id()};

    min_resp_lsn = std::min(min_resp_lsn, row_message_pb.pg_lsn());
    max_resp_lsn = std::max(max_resp_lsn, row_message_pb.pg_lsn());
    min_txn_id = std::min(min_txn_id, row_message_pb.pg_transaction_id());
    max_txn_id = std::max(max_txn_id, row_message_pb.pg_transaction_id());

    row_idx++;
  }

  *record_batch = static_cast<YbcPgChangeRecordBatch *>(YBCPAlloc(sizeof(YbcPgChangeRecordBatch)));
  new (*record_batch) YbcPgChangeRecordBatch{
      .row_count = row_count,
      .rows = resp_rows,
      .needs_publication_table_list_refresh = needs_publication_table_list_refresh,
      .publication_refresh_time = publication_refresh_time
  };

  if (row_count > 0) {
    VLOG(1) << "Summary of the GetConsistentChangesResponsePB response\n"
            << "min_txn_id: " << min_txn_id << ", max_txn_id: " << max_txn_id
            << ", min_lsn: " << min_resp_lsn << ", max_lsn: " << max_resp_lsn;
  } else {
    VLOG(1) << "Received 0 rows in GetConsistentChangesResponsePB response\n";
  }

  auto time_in_conversion = GetCurrentTimeMicros() - response_to_pg_conversion_start;
  VLOG(1) << "Time spent in converting from QLValuePB to PG datum in PgGate: "
          << time_in_conversion << " us";

  return YBCStatusOK();
}

YbcStatus YBCPgUpdateAndPersistLSN(
    const char* stream_id, YbcPgXLogRecPtr restart_lsn_hint, YbcPgXLogRecPtr confirmed_flush,
    YbcPgXLogRecPtr* restart_lsn) {
  const auto result =
      pgapi->UpdateAndPersistLSN(std::string(stream_id), restart_lsn_hint, confirmed_flush);
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }

  *DCHECK_NOTNULL(restart_lsn) = result->restart_lsn();
  return YBCStatusOK();
}

YbcStatus YBCLocalTablets(YbcPgTabletsDescriptor** tablets, size_t* count) {
  const auto result = pgapi->TabletsMetadata();
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }
  const auto& local_tablets = result.get().tablets();
  *count = local_tablets.size();
  if (!local_tablets.empty()) {
    *tablets = static_cast<YbcPgTabletsDescriptor*>(
        YBCPAlloc(sizeof(YbcPgTabletsDescriptor) * local_tablets.size()));
    YbcPgTabletsDescriptor* dest = *tablets;
    for (const auto& tablet : local_tablets) {
      new (dest) YbcPgTabletsDescriptor {
        .tablet_id = YBCPAllocStdString(tablet.tablet_id()),
        .table_name = YBCPAllocStdString(tablet.table_name()),
        .table_id = YBCPAllocStdString(tablet.table_id()),
        .namespace_name = YBCPAllocStdString(tablet.namespace_name()),
        .table_type = YBCPAllocStdString(HumanReadableTableType(tablet.table_type())),
        .pgschema_name = YBCPAllocStdString(tablet.pgschema_name()),
        .partition_key_start = YBCPAllocStdString(tablet.partition().partition_key_start()),
        .partition_key_start_len = tablet.partition().partition_key_start().size(),
        .partition_key_end = YBCPAllocStdString(tablet.partition().partition_key_end()),
        .partition_key_end_len = tablet.partition().partition_key_end().size(),
        .tablet_data_state = YBCPAllocStdString(TabletDataState_Name(tablet.tablet_data_state()))
      };
      ++dest;
    }
  }
  return YBCStatusOK();
}

YbcStatus YBCServersMetrics(YbcPgServerMetricsInfo** servers_metrics_info, size_t* count) {
  const auto result = pgapi->ServersMetrics();
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }
  const auto& servers_metrics = result.get().servers_metrics();
  *count = servers_metrics.size();
  if (!servers_metrics.empty()) {
    *servers_metrics_info = static_cast<YbcPgServerMetricsInfo*>(
        YBCPAlloc(sizeof(YbcPgServerMetricsInfo) * servers_metrics.size()));
    YbcPgServerMetricsInfo* dest = *servers_metrics_info;
    for (const auto& server_metrics_info : servers_metrics) {
      size_t metrics_count = server_metrics_info.metrics().size();
      YbcMetricsInfo* metrics =
          static_cast<YbcMetricsInfo*>(
              YBCPAlloc(sizeof(YbcMetricsInfo) * metrics_count));

      int metrics_idx = 0;
      for (const auto& metrics_info : server_metrics_info.metrics()) {
        metrics[metrics_idx].name = YBCPAllocStdString(metrics_info.name());
        metrics[metrics_idx].value = YBCPAllocStdString(metrics_info.value());
        metrics_idx++;
      }
      new (dest) YbcPgServerMetricsInfo {
        .uuid = YBCPAllocStdString(server_metrics_info.uuid()),
        .metrics = metrics,
        .metrics_count = metrics_count,
        .status = YBCPAllocStdString(PgMetricsInfoStatus_Name(server_metrics_info.status())),
        .error = YBCPAllocStdString(server_metrics_info.error()),
      };
      ++dest;
    }
  }
  return YBCStatusOK();
}

YbcStatus YBCDatabaseClones(YbcPgDatabaseCloneInfo** database_clones, size_t* count) {
  const auto result = pgapi->GetDatabaseClones();
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }
  const auto& tserver_clone_entries = result.get().database_clones();
  *count = tserver_clone_entries.size();
  if (!tserver_clone_entries.empty()) {
    *database_clones = static_cast<YbcPgDatabaseCloneInfo*>(
        YBCPAlloc(sizeof(YbcPgDatabaseCloneInfo) * tserver_clone_entries.size()));
    YbcPgDatabaseCloneInfo* cur_clone = *database_clones;
    for (const auto& tserver_clone_entry : tserver_clone_entries) {
      new (cur_clone) YbcPgDatabaseCloneInfo{
          .db_id = tserver_clone_entry.db_id(),
          .db_name = YBCPAllocStdString(tserver_clone_entry.db_name()),
          .parent_db_id = tserver_clone_entry.parent_db_id(),
          .parent_db_name = YBCPAllocStdString(tserver_clone_entry.parent_db_name()),
          .state = YBCPAllocStdString(tserver_clone_entry.state()),
          .as_of_time = YBCGetPgCallbacks()->UnixEpochToPostgresEpoch(static_cast<int64_t>(
              HybridTime(tserver_clone_entry.as_of_time()).GetPhysicalValueMicros())),
          .failure_reason = YBCPAllocStdString(tserver_clone_entry.failure_reason()),
      };
      ++cur_clone;
    }
  }
  return YBCStatusOK();
}

bool YBCIsCronLeader() { return pgapi->IsCronLeader(); }

int YBCGetXClusterRole(uint32_t db_oid) {
  auto result = pgapi->GetXClusterRole(db_oid);
  if (result.ok()) {
    return *result;
  }
  return XClusterNamespaceInfoPB_XClusterRole_UNAVAILABLE;
}

YbcStatus YBCSetCronLastMinute(int64_t last_minute) {
  return ToYBCStatus(pgapi->SetCronLastMinute(last_minute));
}

YbcStatus YBCGetCronLastMinute(int64_t* last_minute) {
  const auto result = pgapi->GetCronLastMinute();
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }
  *last_minute = result.get();

  return YBCStatusOK();
}

YbcReadPointHandle YBCPgGetCurrentReadPoint() {
  return pgapi->GetCurrentReadPoint();
}

YbcStatus YBCPgRestoreReadPoint(YbcReadPointHandle read_point) {
  return ToYBCStatus(pgapi->RestoreReadPoint(read_point));
}

YbcStatus YBCPgRegisterSnapshotReadTime(
    uint64_t read_time, bool use_read_time, YbcReadPointHandle* handle) {
  YbcReadPointHandle tmp_handle;
  return ExtractValueFromResult(
      pgapi->RegisterSnapshotReadTime(read_time, use_read_time), handle ? handle : &tmp_handle);
}

void YBCRecordTempRelationDDL() {
  if (YBCRecordTempRelationDDL_hook) {
    YBCRecordTempRelationDDL_hook();
  }
}

void YBCDdlEnableForceCatalogModification() {
  pgapi->DdlEnableForceCatalogModification();
}

uint64_t YBCGetCurrentHybridTimeLsn() {
  return (HybridTime::FromMicros(GetCurrentTimeMicros()).ToUint64());
}

YbcStatus YBCAcquireAdvisoryLock(
    YbcAdvisoryLockId lock_id, YbcAdvisoryLockMode mode, bool wait, bool session) {
  return ToYBCStatus(pgapi->AcquireAdvisoryLock(lock_id, mode, wait, session));
}

YbcStatus YBCReleaseAdvisoryLock(YbcAdvisoryLockId lock_id, YbcAdvisoryLockMode mode) {
  return ToYBCStatus(pgapi->ReleaseAdvisoryLock(lock_id, mode));
}

YbcStatus YBCReleaseAllAdvisoryLocks(uint32_t db_oid) {
  return ToYBCStatus(pgapi->ReleaseAllAdvisoryLocks(db_oid));
}

YbcStatus YBCPgExportSnapshot(
    const YbcPgTxnSnapshot* snapshot, char** snapshot_id, const YbcReadPointHandle* read_point) {
  std::optional<YbcReadPointHandle> read_point_handle;
  if (read_point) {
    read_point_handle = *read_point;
  }
  return ExtractValueFromResult(
      pgapi->ExportSnapshot(*snapshot, read_point_handle),
      [snapshot_id](auto value) { *snapshot_id = YBCPAllocStdString(value); });
}

YbcStatus YBCPgImportSnapshot(const char* snapshot_id, YbcPgTxnSnapshot* snapshot) {
  return ExtractValueFromResult(pgapi->ImportSnapshot({snapshot_id}), snapshot);
}

bool YBCPgHasExportedSnapshots() { return pgapi->HasExportedSnapshots(); }

void YBCPgClearExportedTxnSnapshots() { pgapi->ClearExportedTxnSnapshots(); }

YbcStatus YBCAcquireObjectLock(YbcObjectLockId lock_id, YbcObjectLockMode mode) {
  return ToYBCStatus(pgapi->AcquireObjectLock(lock_id, mode));
}

bool YBCPgYsqlMajorVersionUpgradeInProgress() {
  /*
   * yb_upgrade_to_pg15_completed is only available on the newer code version.
   * So we use yb_major_version_upgrade_compatibility to determine if the YSQL major upgrade is in
   * progress on processes running the older version.
   * We cannot rely on yb_major_version_upgrade_compatibility only, since it will be reset in the
   * Monitoring Phase.
   * DevNote: Keep this in sync with IsYsqlMajorVersionUpgradeInProgress.
   */
  return yb_major_version_upgrade_compatibility > 0 || !yb_upgrade_to_pg15_completed;
}

bool YBCIsBinaryUpgrade() {
  return yb_is_binary_upgrade;
}

void YBCSetBinaryUpgrade(bool value) {
  yb_is_binary_upgrade = value;
}

void YBCRecordTablespaceOid(YbcPgOid db_oid, YbcPgOid table_oid, YbcPgOid tablespace_oid) {
  pgapi->RecordTablespaceOid(db_oid, table_oid, tablespace_oid);
}

void YBCClearTablespaceOid(YbcPgOid db_oid, YbcPgOid table_oid) {
  pgapi->ClearTablespaceOid(db_oid, table_oid);
}

YbcStatus YBCInitTransaction(const YbcPgInitTransactionData *data) {
  return ToYBCStatus(YBCInitTransactionImpl(*data));
}

YbcStatus YBCCommitTransactionIntermediate(const YbcPgInitTransactionData *data) {
  return ToYBCStatus(YBCCommitTransactionIntermediateImpl(*data));
}

} // extern "C"

} // namespace yb::pggate
