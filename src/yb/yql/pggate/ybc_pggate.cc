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
#include <iterator>
#include <limits>
#include <memory>
#include <set>
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

#include "yb/dockv/doc_key.h"
#include "yb/dockv/partition.h"
#include "yb/dockv/pg_key_decoder.h"
#include "yb/dockv/pg_row.h"
#include "yb/dockv/primitive_value.h"
#include "yb/dockv/reader_projection.h"
#include "yb/dockv/value_type.h"

#include "yb/gutil/casts.h"

#include "yb/gutil/walltime.h"
#include "yb/server/clockbound_clock.h"
#include "yb/server/skewed_clock.h"

#include "yb/util/atomic.h"
#include "yb/util/curl_util.h"
#include "yb/util/flags.h"
#include "yb/util/jwt_util.h"
#include "yb/util/result.h"
#include "yb/util/signal_util.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/thread.h"
#include "yb/util/yb_partition.h"

#include "yb/yql/pggate/pg_expr.h"
#include "yb/yql/pggate/pg_gate_fwd.h"
#include "yb/yql/pggate/pg_memctx.h"
#include "yb/yql/pggate/pg_statement.h"
#include "yb/yql/pggate/pg_tabledesc.h"
#include "yb/yql/pggate/pg_value.h"
#include "yb/yql/pggate/pggate.h"
#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pggate/pggate_thread_local_vars.h"
#include "yb/yql/pggate/util/pg_wire.h"
#include "yb/yql/pggate/util/ybc-internal.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

DEFINE_UNKNOWN_int32(pggate_num_connections_to_server, 1,
             "Number of underlying connections to each server from a PostgreSQL backend process. "
             "This overrides the value of --num_connections_to_server.");

DECLARE_int32(num_connections_to_server);

DECLARE_int32(delay_alter_sequence_sec);

DECLARE_int32(client_read_write_timeout_ms);

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

DECLARE_bool(TEST_ash_debug_aux);
DECLARE_bool(TEST_generate_ybrowid_sequentially);
DECLARE_bool(TEST_ysql_log_perdb_allocated_new_objectid);

DECLARE_bool(use_fast_backward_scan);

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

namespace yb::pggate {

//--------------------------------------------------------------------------------------------------
// C++ Implementation.
// All C++ objects and structures in this module are listed in the following namespace.
//--------------------------------------------------------------------------------------------------
namespace {

inline YBCStatus YBCStatusOK() {
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
YBCStatus ExtractValueFromResult(Result<T> result, const Functor& functor) {
  if (result.ok()) {
    functor(std::move(*result));
    return YBCStatusOK();
  }
  return ToYBCStatus(result.status());
}

template<class T>
YBCStatus ExtractValueFromResult(Result<T> result, T* value) {
  return ExtractValueFromResult(std::move(result), [value](T src) {
    *value = std::move(src);
  });
}

template<class Processor>
Status ProcessYbctidImpl(const YBCPgYBTupleIdDescriptor& source, const Processor& processor) {
  auto ybctid = VERIFY_RESULT(pgapi->BuildTupleId(source));
  return processor(source.table_relfilenode_oid, ybctid.AsSlice());
}

template<class Processor>
YBCStatus ProcessYbctid(const YBCPgYBTupleIdDescriptor& source, const Processor& processor) {
  return ToYBCStatus(ProcessYbctidImpl(source, processor));
}

Slice YbctidAsSlice(uint64_t ybctid) {
  return pgapi->GetYbctidAsSlice(ybctid);
}

inline std::optional<Bound> MakeBound(YBCPgBoundType type, uint16_t value) {
  if (type == YB_YQL_BOUND_INVALID) {
    return std::nullopt;
  }
  return Bound{.value = value, .is_inclusive = (type == YB_YQL_BOUND_VALID_INCLUSIVE)};
}

Status InitPgGateImpl(const YBCPgTypeEntity* data_type_table,
                      int count,
                      const PgCallbacks& pg_callbacks,
                      uint64_t *session_id,
                      const YBCPgAshConfig* ash_config) {
  auto opt_session_id = session_id ? std::optional(*session_id) : std::nullopt;
  return WithMaskedYsqlSignals(
    [data_type_table, count, &pg_callbacks, opt_session_id, &ash_config] {
    YBCInitPgGateEx(
        data_type_table, count, pg_callbacks, nullptr /* context */, opt_session_id, ash_config);
    return static_cast<Status>(Status::OK());
  });
}

Status PgInitSessionImpl(YBCPgExecStatsState& session_stats, bool is_binary_upgrade) {
  return WithMaskedYsqlSignals([&session_stats, is_binary_upgrade] {
    return pgapi->InitSession(session_stats, is_binary_upgrade);
  });
}

// ql_value is modified in-place.
dockv::PgValue DecodeCollationEncodedString(dockv::PgTableRow* row, Slice value, size_t idx) {
  auto body = pggate::DecodeCollationEncodedString(value);
  return row->TrimString(idx, body.cdata() - value.cdata(), body.size());
}

Status GetSplitPoints(YBCPgTableDesc table_desc,
                      const YBCPgTypeEntity **type_entities,
                      YBCPgTypeAttrs *type_attrs_arr,
                      YBCPgSplitDatum *split_datums,
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

PrefetchingCacheMode YBCMapPrefetcherCacheMode(YBCPgSysTablePrefetcherCacheMode mode) {
  switch (mode) {
    case YB_YQL_PREFETCHER_TRUST_CACHE:
      return PrefetchingCacheMode::TRUST_CACHE;
    case YB_YQL_PREFETCHER_RENEW_CACHE_SOFT:
      return PrefetchingCacheMode::RENEW_CACHE_SOFT;
    case YB_YQL_PREFETCHER_RENEW_CACHE_HARD:
      LOG(DFATAL) << "Emergency fallback prefetching cache mode is used";
      return PrefetchingCacheMode::RENEW_CACHE_HARD;
  }
  LOG(DFATAL) << "Unexpected PgSysTablePrefetcherCacheMode value " << mode;
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

uint32_t AshEncodeWaitStateCodeWithComponent(uint32_t component, uint32_t code) {
  DCHECK_EQ((component >> YB_ASH_COMPONENT_BITS), 0);
  DCHECK_EQ((code >> YB_ASH_COMPONENT_POSITION), 0);
  return (component << YB_ASH_COMPONENT_POSITION) | code;
}

void AshCopyAuxInfo(
    const WaitStateInfoPB& tserver_sample, uint32_t component, YBCAshSample* cb_sample) {
  // Copy the entire aux info, or the first 15 bytes, whichever is smaller.
  // This expects compilation with -Wno-format-truncation.
  const auto& tserver_aux_info = tserver_sample.aux_info();
  snprintf(
      cb_sample->aux_info, sizeof(cb_sample->aux_info), "%s",
      FLAGS_TEST_ash_debug_aux ? tserver_aux_info.method().c_str()
                               : (component == to_underlying(ash::Component::kYCQL)
                                      ? tserver_aux_info.table_id().c_str()
                                      : tserver_aux_info.tablet_id().c_str()));
}

void AshCopyTServerSample(
    YBCAshSample* cb_sample, uint32_t component, const WaitStateInfoPB& tserver_sample,
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
      AshEncodeWaitStateCodeWithComponent(component, tserver_sample.wait_state_code());
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
    YBCAshGetNextCircularBufferSlot get_cb_slot_fn, const tserver::WaitStatesPB& samples,
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

const YBCPgTypeEntity* GetTypeEntity(
    int pg_type_oid, int attr_num, YBCPgOid table_oid, YBCTypeEntityProvider type_entity_provider) {

  // TODO(23239): Optimize the lookup of type entities for dynamic types.
  return pg_type_oid == kPgInvalidOid ? (*type_entity_provider)(attr_num, table_oid)
                                      : pgapi->FindTypeEntity(pg_type_oid);
}

Status YBCGetTableKeyRangesImpl(
    const PgObjectId& table_id, Slice lower_bound_key, Slice upper_bound_key,
    uint64_t max_num_ranges, uint64_t range_size_bytes, bool is_forward, uint32_t max_key_length,
    YBCGetTableKeyRangesCallback callback, void* callback_param) {
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

inline YBCPgExplicitRowLockStatus MakePgExplicitRowLockStatus() {
  return {
      .ybc_status = YBCStatusOK(),
      .error_info = {.is_initialized = false,
                     .pg_wait_policy = 0,
                     .conflicting_table_id = kInvalidOid}};
}

} // namespace

//--------------------------------------------------------------------------------------------------
// C API.
//--------------------------------------------------------------------------------------------------

void YBCInitPgGateEx(const YBCPgTypeEntity *data_type_table, int count, PgCallbacks pg_callbacks,
                     PgApiContext* context, std::optional<uint64_t> session_id,
                     const YBCPgAshConfig* ash_config) {
  // TODO: We should get rid of hybrid clock usage in YSQL backend processes (see #16034).
  // However, this is added to allow simulating and testing of some known bugs until we remove
  // HybridClock usage.
  server::SkewedClock::Register();
  server::RegisterClockboundClockProvider();

  InitThreading();

  CHECK(pgapi == nullptr) << ": " << __PRETTY_FUNCTION__ << " can only be called once";

  YBCInitFlags();

#ifndef NDEBUG
  HybridTime::TEST_SetPrettyToString(true);
#endif

  pgapi_shutdown_done.exchange(false);
  if (context) {
    pgapi = new pggate::PgApiImpl(
        std::move(*context), data_type_table, count, pg_callbacks, session_id, *ash_config);
  } else {
    pgapi = new pggate::PgApiImpl(
        PgApiContext(), data_type_table, count, pg_callbacks, session_id, *ash_config);
  }

  VLOG(1) << "PgGate open";
}

extern "C" {

void YBCInitPgGate(const YBCPgTypeEntity *data_type_table, int count, PgCallbacks pg_callbacks,
                   uint64_t *session_id, const YBCPgAshConfig* ash_config) {
  CHECK_OK(InitPgGateImpl(data_type_table, count, pg_callbacks, session_id, ash_config));
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

const YBCPgCallbacks *YBCGetPgCallbacks() {
  return pgapi->pg_callbacks();
}

YBCStatus YBCValidateJWT(const char *token, const YBCPgJwtAuthOptions *options) {
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

YBCStatus YBCFetchFromUrl(const char *url, char **buf) {
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

void YBCDumpCurrentPgSessionState(YBCPgSessionState* session_data) {
  pgapi->DumpSessionState(session_data);
}

void YBCRestorePgSessionState(const YBCPgSessionState* session_data) {
  CHECK_NOTNULL(pgapi);
  pgapi->RestoreSessionState(*session_data);
}

YBCStatus YBCPgInitSession(YBCPgExecStatsState* session_stats, bool is_binary_upgrade) {
  return ToYBCStatus(PgInitSessionImpl(*session_stats, is_binary_upgrade));
}

uint64_t YBCPgGetSessionID() { return pgapi->GetSessionID(); }

YBCPgMemctx YBCPgCreateMemctx() {
  return pgapi->CreateMemctx();
}

YBCStatus YBCPgDestroyMemctx(YBCPgMemctx memctx) {
  return ToYBCStatus(pgapi->DestroyMemctx(memctx));
}

void YBCPgResetCatalogReadTime() {
  return pgapi->ResetCatalogReadTime();
}

YBCStatus YBCPgResetMemctx(YBCPgMemctx memctx) {
  return ToYBCStatus(pgapi->ResetMemctx(memctx));
}

void YBCPgDeleteStatement(YBCPgStatement handle) {
  pgapi->DeleteStatement(handle);
}

YBCStatus YBCPgInvalidateCache() {
  return ToYBCStatus(pgapi->InvalidateCache());
}

const YBCPgTypeEntity *YBCPgFindTypeEntity(int type_oid) {
  return pgapi->FindTypeEntity(type_oid);
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

YBCStatus YBCGetHeapConsumption(YbTcmallocStats *desc) {
  memset(desc, 0x0, sizeof(YbTcmallocStats));
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

//--------------------------------------------------------------------------------------------------
// YB Bitmap Scan Operations
//--------------------------------------------------------------------------------------------------

typedef std::unordered_set<Slice, Slice::Hash> UnorderedSliceSet;

static void FreeSlice(Slice slice) {
  delete[] slice.data(), slice.size();
}

SliceSet YBCBitmapCreateSet() {
  UnorderedSliceSet *set = new UnorderedSliceSet();
  return set;
}

size_t YBCBitmapUnionSet(SliceSet sa, ConstSliceSet sb) {
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

SliceSet YBCBitmapIntersectSet(SliceSet sa, SliceSet sb) {
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

size_t YBCBitmapInsertYbctidsIntoSet(SliceSet set, ConstSliceVector vec) {
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

ConstSliceVector YBCBitmapCopySetToVector(ConstSliceSet set, size_t *size) {
  const UnorderedSliceSet *s = reinterpret_cast<const UnorderedSliceSet *>(set);
  if (size)
    *size = s->size();
  return new std::vector<Slice>(s->begin(), s->end());
}

ConstSliceVector YBCBitmapGetVectorRange(ConstSliceVector vec, size_t start, size_t length) {
  const std::vector<Slice> *v = reinterpret_cast<const std::vector<Slice> *>(vec);

  const size_t end_index = std::min(start + length, v->size());

  if (end_index <= start)
    return NULL;

  return new std::vector<Slice>(v->begin() + start, v->begin() + end_index);
}

void YBCBitmapShallowDeleteVector(ConstSliceVector vec) {
  delete reinterpret_cast<const std::vector<Slice> *>(vec);
}

void YBCBitmapShallowDeleteSet(ConstSliceVector set) {
  delete reinterpret_cast<const UnorderedSliceSet *>(set);
}

void YBCBitmapDeepDeleteSet(ConstSliceSet set) {
  const UnorderedSliceSet *s = reinterpret_cast<const UnorderedSliceSet *>(set);
  for (auto &slice : *s)
    FreeSlice(slice);
  delete s;
}

size_t YBCBitmapGetSetSize(ConstSliceSet set) {
  if (!set)
    return 0;
  const UnorderedSliceSet *s = reinterpret_cast<const UnorderedSliceSet *>(set);
  return s->size();
}

size_t YBCBitmapGetVectorSize(ConstSliceVector vec) {
  if (!vec)
    return 0;
  const std::vector<Slice> *v = reinterpret_cast<const std::vector<Slice> *>(vec);
  return v->size();
}

//--------------------------------------------------------------------------------------------------
// DDL Statements.
//--------------------------------------------------------------------------------------------------
// Database Operations -----------------------------------------------------------------------------

YBCStatus YBCPgIsDatabaseColocated(const YBCPgOid database_oid, bool *colocated,
                                   bool *legacy_colocated_database) {
  return ToYBCStatus(pgapi->IsDatabaseColocated(database_oid, colocated,
                                                legacy_colocated_database));
}

YBCStatus YBCPgNewCreateDatabase(
    const char* database_name, const YBCPgOid database_oid, const YBCPgOid source_database_oid,
    const YBCPgOid next_oid, const bool colocated, YbCloneInfo *yb_clone_info,
    YBCPgStatement* handle) {
  return ToYBCStatus(pgapi->NewCreateDatabase(
      database_name, database_oid, source_database_oid, next_oid, colocated,
      yb_clone_info, handle));
}

YBCStatus YBCPgExecCreateDatabase(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecCreateDatabase(handle));
}

YBCStatus YBCPgNewDropDatabase(const char *database_name,
                               const YBCPgOid database_oid,
                               YBCPgStatement *handle) {
  return ToYBCStatus(pgapi->NewDropDatabase(database_name, database_oid, handle));
}

YBCStatus YBCPgExecDropDatabase(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecDropDatabase(handle));
}

YBCStatus YBCPgNewAlterDatabase(const char *database_name,
                               const YBCPgOid database_oid,
                               YBCPgStatement *handle) {
  return ToYBCStatus(pgapi->NewAlterDatabase(database_name, database_oid, handle));
}

YBCStatus YBCPgAlterDatabaseRenameDatabase(YBCPgStatement handle, const char *newname) {
  return ToYBCStatus(pgapi->AlterDatabaseRenameDatabase(handle, newname));
}

YBCStatus YBCPgExecAlterDatabase(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecAlterDatabase(handle));
}

YBCStatus YBCPgReserveOids(const YBCPgOid database_oid,
                           const YBCPgOid next_oid,
                           const uint32_t count,
                           YBCPgOid *begin_oid,
                           YBCPgOid *end_oid) {
  return ToYBCStatus(pgapi->ReserveOids(database_oid, next_oid,
                                        count, begin_oid, end_oid));
}

YBCStatus YBCGetNewObjectId(YBCPgOid db_oid, YBCPgOid* new_oid) {
  DCHECK_NE(db_oid, kInvalidOid);
  return ToYBCStatus(pgapi->GetNewObjectId(db_oid, new_oid));
}

YBCStatus YBCPgGetCatalogMasterVersion(uint64_t *version) {
  return ToYBCStatus(pgapi->GetCatalogMasterVersion(version));
}

YBCStatus YBCPgInvalidateTableCacheByTableId(const char *table_id) {
  if (table_id == NULL) {
    return ToYBCStatus(STATUS(InvalidArgument, "table_id is null"));
  }
  std::string table_id_str = table_id;
  const PgObjectId pg_object_id(table_id_str);
  pgapi->InvalidateTableCache(pg_object_id);
  return YBCStatusOK();
}

void YBCPgAlterTableInvalidateTableByOid(
    const YBCPgOid database_oid, const YBCPgOid table_relfilenode_oid) {
  pgapi->InvalidateTableCache(PgObjectId(database_oid, table_relfilenode_oid));
}

// Tablegroup Operations ---------------------------------------------------------------------------

YBCStatus YBCPgNewCreateTablegroup(const char *database_name,
                                   YBCPgOid database_oid,
                                   YBCPgOid tablegroup_oid,
                                   YBCPgOid tablespace_oid,
                                   YBCPgStatement *handle) {
  return ToYBCStatus(pgapi->NewCreateTablegroup(database_name,
                                                database_oid,
                                                tablegroup_oid,
                                                tablespace_oid,
                                                handle));
}

YBCStatus YBCPgExecCreateTablegroup(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecCreateTablegroup(handle));
}

YBCStatus YBCPgNewDropTablegroup(YBCPgOid database_oid,
                                 YBCPgOid tablegroup_oid,
                                 YBCPgStatement *handle) {
  return ToYBCStatus(pgapi->NewDropTablegroup(database_oid,
                                              tablegroup_oid,
                                              handle));
}
YBCStatus YBCPgExecDropTablegroup(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecDropTablegroup(handle));
}

// Sequence Operations -----------------------------------------------------------------------------

YBCStatus YBCInsertSequenceTuple(int64_t db_oid,
                                 int64_t seq_oid,
                                 uint64_t ysql_catalog_version,
                                 bool is_db_catalog_version_mode,
                                 int64_t last_val,
                                 bool is_called) {
  return ToYBCStatus(pgapi->InsertSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, is_db_catalog_version_mode, last_val, is_called));
}

YBCStatus YBCUpdateSequenceTupleConditionally(int64_t db_oid,
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

YBCStatus YBCUpdateSequenceTuple(int64_t db_oid,
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

YBCStatus YBCFetchSequenceTuple(int64_t db_oid,
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

YBCStatus YBCReadSequenceTuple(int64_t db_oid,
                               int64_t seq_oid,
                               uint64_t ysql_catalog_version,
                               bool is_db_catalog_version_mode,
                               int64_t *last_val,
                               bool *is_called) {
  return ToYBCStatus(pgapi->ReadSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, is_db_catalog_version_mode, last_val, is_called));
}

YBCStatus YBCPgNewDropSequence(const YBCPgOid database_oid,
                               const YBCPgOid sequence_oid,
                               YBCPgStatement *handle) {
  return ToYBCStatus(pgapi->NewDropSequence(database_oid, sequence_oid, handle));
}

YBCStatus YBCPgExecDropSequence(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecDropSequence(handle));
}

YBCStatus YBCPgNewDropDBSequences(const YBCPgOid database_oid,
                                  YBCPgStatement *handle) {
  return ToYBCStatus(pgapi->NewDropDBSequences(database_oid, handle));
}

// Table Operations -------------------------------------------------------------------------------

YBCStatus YBCPgNewCreateTable(const char *database_name,
                              const char *schema_name,
                              const char *table_name,
                              YBCPgOid database_oid,
                              YBCPgOid table_relfilenode_oid,
                              bool is_shared_table,
                              bool is_sys_catalog_table,
                              bool if_not_exist,
                              YBCPgYbrowidMode ybrowid_mode,
                              bool is_colocated_via_database,
                              YBCPgOid tablegroup_oid,
                              YBCPgOid colocation_id,
                              YBCPgOid tablespace_oid,
                              bool is_matview,
                              YBCPgOid pg_table_oid,
                              YBCPgOid old_relfilenode_oid,
                              bool is_truncate,
                              YBCPgStatement *handle) {
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

YBCStatus YBCPgCreateTableAddColumn(YBCPgStatement handle, const char *attr_name, int attr_num,
                                    const YBCPgTypeEntity *attr_type, bool is_hash, bool is_range,
                                    bool is_desc, bool is_nulls_first) {
  return ToYBCStatus(pgapi->CreateTableAddColumn(handle, attr_name, attr_num, attr_type,
                                                 is_hash, is_range, is_desc, is_nulls_first));
}

YBCStatus YBCPgCreateTableSetNumTablets(YBCPgStatement handle, int32_t num_tablets) {
  return ToYBCStatus(pgapi->CreateTableSetNumTablets(handle, num_tablets));
}

YBCStatus YBCPgAddSplitBoundary(YBCPgStatement handle, YBCPgExpr *exprs, int expr_count) {
  return ToYBCStatus(pgapi->AddSplitBoundary(handle, exprs, expr_count));
}

YBCStatus YBCPgExecCreateTable(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecCreateTable(handle));
}

YBCStatus YBCPgNewAlterTable(const YBCPgOid database_oid,
                             const YBCPgOid table_relfilenode_oid,
                             YBCPgStatement *handle) {
  const PgObjectId table_id(database_oid, table_relfilenode_oid);
  return ToYBCStatus(pgapi->NewAlterTable(table_id, handle));
}

YBCStatus YBCPgAlterTableAddColumn(YBCPgStatement handle, const char *name, int order,
                                   const YBCPgTypeEntity *attr_type, YBCPgExpr missing_value) {
  return ToYBCStatus(pgapi->AlterTableAddColumn(handle, name, order, attr_type, missing_value));
}

YBCStatus YBCPgAlterTableRenameColumn(YBCPgStatement handle, const char *oldname,
                                      const char *newname) {
  return ToYBCStatus(pgapi->AlterTableRenameColumn(handle, oldname, newname));
}

YBCStatus YBCPgAlterTableDropColumn(YBCPgStatement handle, const char *name) {
  return ToYBCStatus(pgapi->AlterTableDropColumn(handle, name));
}

YBCStatus YBCPgAlterTableSetReplicaIdentity(YBCPgStatement handle, const char identity_type) {
  return ToYBCStatus(pgapi->AlterTableSetReplicaIdentity(handle, identity_type));
}

YBCStatus YBCPgAlterTableRenameTable(YBCPgStatement handle, const char *db_name,
                                     const char *newname) {
  return ToYBCStatus(pgapi->AlterTableRenameTable(handle, db_name, newname));
}

YBCStatus YBCPgAlterTableIncrementSchemaVersion(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->AlterTableIncrementSchemaVersion(handle));
}

YBCStatus YBCPgAlterTableSetTableId(
    YBCPgStatement handle, const YBCPgOid database_oid, const YBCPgOid table_relfilenode_oid) {
  const PgObjectId table_id(database_oid, table_relfilenode_oid);
  return ToYBCStatus(pgapi->AlterTableSetTableId(handle, table_id));
}

YBCStatus YBCPgAlterTableSetSchema(YBCPgStatement handle, const char *schema_name) {
  return ToYBCStatus(pgapi->AlterTableSetSchema(handle, schema_name));
}

YBCStatus YBCPgExecAlterTable(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecAlterTable(handle));
}

YBCStatus YBCPgAlterTableInvalidateTableCacheEntry(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->AlterTableInvalidateTableCacheEntry(handle));
}

YBCStatus YBCPgNewDropTable(const YBCPgOid database_oid,
                            const YBCPgOid table_relfilenode_oid,
                            bool if_exist,
                            YBCPgStatement *handle) {
  const PgObjectId table_id(database_oid, table_relfilenode_oid);
  return ToYBCStatus(pgapi->NewDropTable(table_id, if_exist, handle));
}

YBCStatus YBCPgGetTableDesc(const YBCPgOid database_oid,
                            const YBCPgOid table_relfilenode_oid,
                            YBCPgTableDesc *handle) {
  const PgObjectId table_id(database_oid, table_relfilenode_oid);
  return ToYBCStatus(pgapi->GetTableDesc(table_id, handle));
}

YBCStatus YBCPgGetColumnInfo(YBCPgTableDesc table_desc,
                             int16_t attr_number,
                             YBCPgColumnInfo *column_info) {
  return ExtractValueFromResult(pgapi->GetColumnInfo(table_desc, attr_number), column_info);
}

YBCStatus YBCPgSetCatalogCacheVersion(YBCPgStatement handle, uint64_t version) {
  return ToYBCStatus(pgapi->SetCatalogCacheVersion(handle, version));
}

YBCStatus YBCPgSetDBCatalogCacheVersion(
    YBCPgStatement handle, YBCPgOid db_oid, uint64_t version) {
  return ToYBCStatus(pgapi->SetCatalogCacheVersion(handle, version, db_oid));
}

YBCStatus YBCPgDmlModifiesRow(YBCPgStatement handle, bool *modifies_row) {
  return ExtractValueFromResult(pgapi->DmlModifiesRow(handle), modifies_row);
}

YBCStatus YBCPgSetIsSysCatalogVersionChange(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->SetIsSysCatalogVersionChange(handle));
}

YBCStatus YBCPgNewTruncateTable(const YBCPgOid database_oid,
                                const YBCPgOid table_relfilenode_oid,
                                YBCPgStatement *handle) {
  const PgObjectId table_id(database_oid, table_relfilenode_oid);
  return ToYBCStatus(pgapi->NewTruncateTable(table_id, handle));
}

YBCStatus YBCPgExecTruncateTable(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecTruncateTable(handle));
}

YBCStatus YBCPgGetTableProperties(YBCPgTableDesc table_desc,
                                  YbTableProperties properties) {
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
YBCStatus YBCGetSplitPoints(YBCPgTableDesc table_desc,
                            const YBCPgTypeEntity **type_entities,
                            YBCPgTypeAttrs *type_attrs_arr,
                            YBCPgSplitDatum *split_datums,
                            bool *has_null, bool *has_gin_null) {
  return ToYBCStatus(GetSplitPoints(table_desc, type_entities, type_attrs_arr, split_datums,
                                    has_null, has_gin_null));
}

YBCStatus YBCPgTableExists(const YBCPgOid database_oid,
                           const YBCPgOid table_relfilenode_oid,
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

YBCStatus YBCPgGetTableDiskSize(YBCPgOid table_relfilenode_oid,
                                YBCPgOid database_oid,
                                int64_t *size,
                                int32_t *num_missing_tablets) {
  return ExtractValueFromResult(pgapi->GetTableDiskSize({database_oid, table_relfilenode_oid}),
                                [size, num_missing_tablets](auto value) {
     *size = value.table_size;
     *num_missing_tablets = value.num_missing_tablets;
  });
}

// Index Operations -------------------------------------------------------------------------------

YBCStatus YBCPgNewCreateIndex(const char *database_name,
                              const char *schema_name,
                              const char *index_name,
                              const YBCPgOid database_oid,
                              const YBCPgOid index_oid,
                              const YBCPgOid table_relfilenode_oid,
                              bool is_shared_index,
                              bool is_sys_catalog_index,
                              bool is_unique_index,
                              const bool skip_index_backfill,
                              bool if_not_exist,
                              bool is_colocated_via_database,
                              const YBCPgOid tablegroup_oid,
                              const YBCPgOid colocation_id,
                              const YBCPgOid tablespace_oid,
                              const YBCPgOid pg_table_oid,
                              const YBCPgOid old_relfilenode_oid,
                              YBCPgStatement *handle) {
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

YBCStatus YBCPgCreateIndexAddColumn(YBCPgStatement handle, const char *attr_name, int attr_num,
                                    const YBCPgTypeEntity *attr_type, bool is_hash, bool is_range,
                                    bool is_desc, bool is_nulls_first) {
  return ToYBCStatus(pgapi->CreateIndexAddColumn(handle, attr_name, attr_num, attr_type,
                                                 is_hash, is_range, is_desc, is_nulls_first));
}

YBCStatus YBCPgCreateIndexSetNumTablets(YBCPgStatement handle, int32_t num_tablets) {
  return ToYBCStatus(pgapi->CreateIndexSetNumTablets(handle, num_tablets));
}

YBCStatus YBCPgCreateIndexSetVectorOptions(YBCPgStatement handle, YbPgVectorIdxOptions *options) {
  return ToYBCStatus(pgapi->CreateIndexSetVectorOptions(handle, options));
}

YBCStatus YBCPgExecCreateIndex(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecCreateIndex(handle));
}

YBCStatus YBCPgNewDropIndex(const YBCPgOid database_oid,
                            const YBCPgOid index_oid,
                            bool if_exist,
                            bool ddl_rollback_enabled,
                            YBCPgStatement *handle) {
  const PgObjectId index_id(database_oid, index_oid);
  return ToYBCStatus(pgapi->NewDropIndex(index_id, if_exist, ddl_rollback_enabled, handle));
}

YBCStatus YBCPgExecPostponedDdlStmt(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecPostponedDdlStmt(handle));
}

YBCStatus YBCPgExecDropTable(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecDropTable(handle));
}

YBCStatus YBCPgExecDropIndex(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecDropIndex(handle));
}

YBCStatus YBCPgWaitForBackendsCatalogVersion(YBCPgOid dboid, uint64_t version, pid_t pid,
                                             int* num_lagging_backends) {
  return ExtractValueFromResult(pgapi->WaitForBackendsCatalogVersion(dboid, version, pid),
                                num_lagging_backends);
}

YBCStatus YBCPgBackfillIndex(
    const YBCPgOid database_oid,
    const YBCPgOid index_oid) {
  const PgObjectId index_id(database_oid, index_oid);
  return ToYBCStatus(pgapi->BackfillIndex(index_id));
}

//--------------------------------------------------------------------------------------------------
// DML Statements.
//--------------------------------------------------------------------------------------------------

YBCStatus YBCPgDmlAppendTarget(YBCPgStatement handle, YBCPgExpr target) {
  return ToYBCStatus(pgapi->DmlAppendTarget(handle, target));
}

YBCStatus YbPgDmlAppendQual(YBCPgStatement handle, YBCPgExpr qual, bool is_for_secondary_index) {
  return ToYBCStatus(pgapi->DmlAppendQual(handle, qual, is_for_secondary_index));
}

YBCStatus YbPgDmlAppendColumnRef(
    YBCPgStatement handle, YBCPgExpr colref, bool is_for_secondary_index) {
  return ToYBCStatus(pgapi->DmlAppendColumnRef(
      handle, down_cast<PgColumnRef*>(colref), is_for_secondary_index));
}

YBCStatus YBCPgDmlBindColumn(YBCPgStatement handle, int attr_num, YBCPgExpr attr_value) {
  return ToYBCStatus(pgapi->DmlBindColumn(handle, attr_num, attr_value));
}

YBCStatus YBCPgDmlBindRow(
    YBCPgStatement handle, uint64_t ybctid, YBCBindColumn* columns, int count) {
  return ToYBCStatus(pgapi->DmlBindRow(handle, ybctid, columns, count));
}

YBCStatus YBCPgDmlAddRowUpperBound(YBCPgStatement handle,
                                    int n_col_values,
                                    YBCPgExpr *col_values,
                                    bool is_inclusive) {
    return ToYBCStatus(pgapi->DmlAddRowUpperBound(handle,
                                                    n_col_values,
                                                    col_values,
                                                    is_inclusive));
}

YBCStatus YBCPgDmlAddRowLowerBound(YBCPgStatement handle,
                                    int n_col_values,
                                    YBCPgExpr *col_values,
                                    bool is_inclusive) {
    return ToYBCStatus(pgapi->DmlAddRowLowerBound(handle,
                                                    n_col_values,
                                                    col_values,
                                                    is_inclusive));
}

YBCStatus YBCPgDmlBindColumnCondBetween(YBCPgStatement handle,
                                        int attr_num,
                                        YBCPgExpr attr_value,
                                        bool start_inclusive,
                                        YBCPgExpr attr_value_end,
                                        bool end_inclusive) {
  return ToYBCStatus(pgapi->DmlBindColumnCondBetween(handle,
                                                     attr_num,
                                                     attr_value,
                                                     start_inclusive,
                                                     attr_value_end,
                                                     end_inclusive));
}

YBCStatus YBCPgDmlBindColumnCondIsNotNull(YBCPgStatement handle, int attr_num) {
  return ToYBCStatus(pgapi->DmlBindColumnCondIsNotNull(handle, attr_num));
}

YBCStatus YBCPgDmlBindColumnCondIn(YBCPgStatement handle, YBCPgExpr lhs, int n_attr_values,
                                   YBCPgExpr *attr_values) {
  return ToYBCStatus(pgapi->DmlBindColumnCondIn(handle, lhs, n_attr_values, attr_values));
}

YBCStatus YBCPgDmlBindHashCodes(
  YBCPgStatement handle,
  YBCPgBoundType start_type, uint16_t start_value,
  YBCPgBoundType end_type, uint16_t end_value) {
  const auto start = MakeBound(start_type, start_value);
  const auto end = MakeBound(end_type, end_value);
  DCHECK(start || end);
  return ToYBCStatus(pgapi->DmlBindHashCode(handle, start, end));
}

YBCStatus YBCPgDmlBindRange(YBCPgStatement handle,
                            const char *lower_bound, size_t lower_bound_len,
                            const char *upper_bound, size_t upper_bound_len) {
  return ToYBCStatus(pgapi->DmlBindRange(
    handle, Slice(lower_bound, lower_bound_len), true,
            Slice(upper_bound, upper_bound_len), false));
}

YBCStatus YBCPgDmlBindTable(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->DmlBindTable(handle));
}

YBCStatus YBCPgDmlGetColumnInfo(YBCPgStatement handle, int attr_num, YBCPgColumnInfo* column_info) {
  return ExtractValueFromResult(pgapi->DmlGetColumnInfo(handle, attr_num), column_info);
}

YBCStatus YBCPgDmlAssignColumn(YBCPgStatement handle,
                               int attr_num,
                               YBCPgExpr attr_value) {
  return ToYBCStatus(pgapi->DmlAssignColumn(handle, attr_num, attr_value));
}

YBCStatus YBCPgDmlANNBindVector(YBCPgStatement handle, YBCPgExpr vector) {
  return ToYBCStatus(pgapi->DmlANNBindVector(handle, vector));
}

YBCStatus YBCPgDmlANNSetPrefetchSize(YBCPgStatement handle, int prefetch_size) {
  return ToYBCStatus(pgapi->DmlANNSetPrefetchSize(handle, prefetch_size));
}

YBCStatus YBCPgDmlFetch(YBCPgStatement handle, int32_t natts, uint64_t *values, bool *isnulls,
                        YBCPgSysColumns *syscols, bool *has_data) {
  return ToYBCStatus(pgapi->DmlFetch(handle, natts, values, isnulls, syscols, has_data));
}

YBCStatus YBCPgStartOperationsBuffering() {
  return ToYBCStatus(pgapi->StartOperationsBuffering());
}

YBCStatus YBCPgStopOperationsBuffering() {
  return ToYBCStatus(pgapi->StopOperationsBuffering());
}

void YBCPgResetOperationsBuffering() {
  pgapi->ResetOperationsBuffering();
}

YBCStatus YBCPgFlushBufferedOperations() {
  return ToYBCStatus(pgapi->FlushBufferedOperations());
}

YBCStatus YBCPgDmlExecWriteOp(YBCPgStatement handle, int32_t *rows_affected_count) {
  return ToYBCStatus(pgapi->DmlExecWriteOp(handle, rows_affected_count));
}

YBCStatus YBCPgBuildYBTupleId(const YBCPgYBTupleIdDescriptor *source, uint64_t *ybctid) {
  return ProcessYbctid(*source, [ybctid](const auto&, const auto& yid) {
    const auto* type_entity = pgapi->FindTypeEntity(kByteArrayOid);
    *ybctid = type_entity->yb_to_datum(yid.cdata(), yid.size(), nullptr /* type_attrs */);
    return Status::OK();
  });
}

YBCStatus YBCPgNewSample(
    const YBCPgOid database_oid, const YBCPgOid table_relfilenode_oid, bool is_region_local,
    int targrows, double rstate_w, uint64_t rand_state_s0, uint64_t rand_state_s1,
    YBCPgStatement *handle) {
  return ToYBCStatus(pgapi->NewSample(
      {database_oid, table_relfilenode_oid}, is_region_local,
      targrows, {.w = rstate_w, .s0 = rand_state_s0, .s1 = rand_state_s1}, handle));
}

YBCStatus YBCPgSampleNextBlock(YBCPgStatement handle, bool *has_more) {
  return ExtractValueFromResult(pgapi->SampleNextBlock(handle), has_more);
}

YBCStatus YBCPgExecSample(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecSample(handle));
}

YBCStatus YBCPgGetEstimatedRowCount(YBCPgStatement handle, double *liverows, double *deadrows) {
  return ExtractValueFromResult(
      pgapi->GetEstimatedRowCount(handle),
      [liverows, deadrows](const auto& count) {
        *liverows = count.live;
        *deadrows = count.dead;
      });
}

// INSERT Operations -------------------------------------------------------------------------------
YBCStatus YBCPgNewInsertBlock(
    YBCPgOid database_oid,
    YBCPgOid table_oid,
    bool is_region_local,
    YBCPgTransactionSetting transaction_setting,
    YBCPgStatement *handle) {
  auto result = pgapi->NewInsertBlock(
      PgObjectId(database_oid, table_oid), is_region_local, transaction_setting);
  if (result.ok()) {
    *handle = *result;
    return NULL;
  }
  return ToYBCStatus(result.status());
}

YBCStatus YBCPgNewInsert(const YBCPgOid database_oid,
                         const YBCPgOid table_relfilenode_oid,
                         bool is_region_local,
                         YBCPgStatement *handle,
                         YBCPgTransactionSetting transaction_setting) {
  const PgObjectId table_id(database_oid, table_relfilenode_oid);
  return ToYBCStatus(pgapi->NewInsert(table_id, is_region_local, handle, transaction_setting));
}

YBCStatus YBCPgExecInsert(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecInsert(handle));
}

YBCStatus YBCPgInsertStmtSetUpsertMode(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->InsertStmtSetUpsertMode(handle));
}

YBCStatus YBCPgInsertStmtSetWriteTime(YBCPgStatement handle, const uint64_t write_time) {
  HybridTime write_hybrid_time;
  YBCStatus status = ToYBCStatus(write_hybrid_time.FromUint64(write_time));
  if (status) {
    return status;
  } else {
    return ToYBCStatus(pgapi->InsertStmtSetWriteTime(handle, write_hybrid_time));
  }
}

YBCStatus YBCPgInsertStmtSetIsBackfill(YBCPgStatement handle, const bool is_backfill) {
  return ToYBCStatus(pgapi->InsertStmtSetIsBackfill(handle, is_backfill));
}

// UPDATE Operations -------------------------------------------------------------------------------
YBCStatus YBCPgNewUpdate(const YBCPgOid database_oid,
                         const YBCPgOid table_relfilenode_oid,
                         bool is_region_local,
                         YBCPgStatement *handle,
                         YBCPgTransactionSetting transaction_setting) {
  const PgObjectId table_id(database_oid, table_relfilenode_oid);
  return ToYBCStatus(pgapi->NewUpdate(table_id, is_region_local, handle, transaction_setting));
}

YBCStatus YBCPgExecUpdate(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecUpdate(handle));
}

// DELETE Operations -------------------------------------------------------------------------------
YBCStatus YBCPgNewDelete(const YBCPgOid database_oid,
                         const YBCPgOid table_relfilenode_oid,
                         bool is_region_local,
                         YBCPgStatement *handle,
                         YBCPgTransactionSetting transaction_setting) {
  const PgObjectId table_id(database_oid, table_relfilenode_oid);
  return ToYBCStatus(pgapi->NewDelete(table_id, is_region_local, handle, transaction_setting));
}

YBCStatus YBCPgExecDelete(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecDelete(handle));
}

YBCStatus YBCPgDeleteStmtSetIsPersistNeeded(YBCPgStatement handle, const bool is_persist_needed) {
  return ToYBCStatus(pgapi->DeleteStmtSetIsPersistNeeded(handle, is_persist_needed));
}

// Colocated TRUNCATE Operations -------------------------------------------------------------------
YBCStatus YBCPgNewTruncateColocated(const YBCPgOid database_oid,
                                    const YBCPgOid table_relfilenode_oid,
                                    bool is_region_local,
                                    YBCPgStatement *handle,
                                    YBCPgTransactionSetting transaction_setting) {
  const PgObjectId table_id(database_oid, table_relfilenode_oid);
  return ToYBCStatus(pgapi->NewTruncateColocated(
      table_id, is_region_local, handle, transaction_setting));
}

YBCStatus YBCPgExecTruncateColocated(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecTruncateColocated(handle));
}

// SELECT Operations -------------------------------------------------------------------------------
YBCStatus YBCPgNewSelect(
    YBCPgOid database_oid, YBCPgOid table_relfilenode_oid, const YBCPgPrepareParameters* params,
    bool is_region_local, YBCPgStatement* handle) {
  return ToYBCStatus(pgapi->NewSelect(
      PgObjectId{database_oid, table_relfilenode_oid},
      PgObjectId{database_oid, params ? params->index_relfilenode_oid : kInvalidOid},
      params, is_region_local, handle));
}

YBCStatus YBCPgSetForwardScan(YBCPgStatement handle, bool is_forward_scan) {
  return ToYBCStatus(pgapi->SetForwardScan(handle, is_forward_scan));
}

YBCStatus YBCPgSetDistinctPrefixLength(YBCPgStatement handle, int distinct_prefix_length) {
  return ToYBCStatus(pgapi->SetDistinctPrefixLength(handle, distinct_prefix_length));
}

YBCStatus YBCPgSetHashBounds(YBCPgStatement handle, uint16_t low_bound, uint16_t high_bound) {
  return ToYBCStatus(pgapi->SetHashBounds(handle, low_bound, high_bound));
}

YBCStatus YBCPgExecSelect(YBCPgStatement handle, const YBCPgExecParameters *exec_params) {
  return ToYBCStatus(pgapi->ExecSelect(handle, exec_params));
}

YBCStatus YBCPgRetrieveYbctids(YBCPgStatement handle, const YBCPgExecParameters *exec_params,
                                   int natts, SliceVector *ybctids, size_t *count,
                                   bool *exceeded_work_mem) {
  return ExtractValueFromResult(
      pgapi->RetrieveYbctids(handle, exec_params, natts, ybctids, count),
      [exceeded_work_mem](bool retrieved) { *exceeded_work_mem = !retrieved; });
}

YBCStatus YBCPgFetchRequestedYbctids(YBCPgStatement handle, const YBCPgExecParameters *exec_params,
                                     ConstSliceVector ybctids) {
  return ToYBCStatus(pgapi->FetchRequestedYbctids(handle, exec_params, ybctids));
}

//------------------------------------------------------------------------------------------------
// Functions
//------------------------------------------------------------------------------------------------

YBCStatus YBCAddFunctionParam(
    YBCPgFunction handle, const char *name, const YBCPgTypeEntity *type_entity, uint64_t datum,
    bool is_null) {
  return ToYBCStatus(
      pgapi->AddFunctionParam(handle, std::string(name), type_entity, datum, is_null));
}

YBCStatus YBCAddFunctionTarget(
    YBCPgFunction handle, const char *attr_name, const YBCPgTypeEntity *type_entity,
    const YBCPgTypeAttrs type_attrs) {
  return ToYBCStatus(
      pgapi->AddFunctionTarget(handle, std::string(attr_name), type_entity, type_attrs));
}

YBCStatus YBCFinalizeFunctionTargets(YBCPgFunction handle) {
  return ToYBCStatus(pgapi->FinalizeFunctionTargets(handle));
}

YBCStatus YBCSRFGetNext(YBCPgFunction handle, uint64_t *values, bool *is_nulls, bool *has_data) {
  return ToYBCStatus(pgapi->SRFGetNext(handle, values, is_nulls, has_data));
}

//--------------------------------------------------------------------------------------------------
// Expression Operations
//--------------------------------------------------------------------------------------------------

YBCStatus YBCPgNewColumnRef(
    YBCPgStatement stmt, int attr_num, const YBCPgTypeEntity *type_entity,
    bool collate_is_valid_non_c, const YBCPgTypeAttrs *type_attrs, YBCPgExpr *expr_handle) {
  return ToYBCStatus(pgapi->NewColumnRef(
      stmt, attr_num, type_entity, collate_is_valid_non_c, type_attrs, expr_handle));
}

YBCStatus YBCPgNewConstant(
    YBCPgStatement stmt, const YBCPgTypeEntity *type_entity, bool collate_is_valid_non_c,
    const char *collation_sortkey, uint64_t datum, bool is_null, YBCPgExpr *expr_handle) {
  return ToYBCStatus(pgapi->NewConstant(
      stmt, type_entity, collate_is_valid_non_c, collation_sortkey, datum, is_null, expr_handle));
}

YBCStatus YBCPgNewConstantVirtual(
    YBCPgStatement stmt, const YBCPgTypeEntity *type_entity,
    YBCPgDatumKind datum_kind, YBCPgExpr *expr_handle) {
  return ToYBCStatus(pgapi->NewConstantVirtual(stmt, type_entity, datum_kind, expr_handle));
}

YBCStatus YBCPgNewConstantOp(
    YBCPgStatement stmt, const YBCPgTypeEntity *type_entity, bool collate_is_valid_non_c,
    const char *collation_sortkey, uint64_t datum, bool is_null, YBCPgExpr *expr_handle,
    bool is_gt) {
  return ToYBCStatus(pgapi->NewConstantOp(
      stmt, type_entity, collate_is_valid_non_c, collation_sortkey, datum, is_null, expr_handle,
      is_gt));
}

// Overwriting the expression's result with any desired values.
YBCStatus YBCPgUpdateConstInt2(YBCPgExpr expr, int16_t value, bool is_null) {
  return ToYBCStatus(pgapi->UpdateConstant(expr, value, is_null));
}

YBCStatus YBCPgUpdateConstInt4(YBCPgExpr expr, int32_t value, bool is_null) {
  return ToYBCStatus(pgapi->UpdateConstant(expr, value, is_null));
}

YBCStatus YBCPgUpdateConstInt8(YBCPgExpr expr, int64_t value, bool is_null) {
  return ToYBCStatus(pgapi->UpdateConstant(expr, value, is_null));
}

YBCStatus YBCPgUpdateConstFloat4(YBCPgExpr expr, float value, bool is_null) {
  return ToYBCStatus(pgapi->UpdateConstant(expr, value, is_null));
}

YBCStatus YBCPgUpdateConstFloat8(YBCPgExpr expr, double value, bool is_null) {
  return ToYBCStatus(pgapi->UpdateConstant(expr, value, is_null));
}

YBCStatus YBCPgUpdateConstText(YBCPgExpr expr, const char *value, bool is_null) {
  return ToYBCStatus(pgapi->UpdateConstant(expr, value, is_null));
}

YBCStatus YBCPgUpdateConstBinary(YBCPgExpr expr, const char *value,  int64_t bytes, bool is_null) {
  return ToYBCStatus(pgapi->UpdateConstant(expr, value, bytes, is_null));
}

YBCStatus YBCPgNewOperator(
    YBCPgStatement stmt, const char *opname, const YBCPgTypeEntity *type_entity,
    bool collate_is_valid_non_c, YBCPgExpr *op_handle) {
  return ToYBCStatus(pgapi->NewOperator(
      stmt, opname, type_entity, collate_is_valid_non_c, op_handle));
}

YBCStatus YBCPgOperatorAppendArg(YBCPgExpr op_handle, YBCPgExpr arg) {
  return ToYBCStatus(pgapi->OperatorAppendArg(op_handle, arg));
}

YBCStatus YBCPgNewTupleExpr(
    YBCPgStatement stmt, const YBCPgTypeEntity *tuple_type_entity,
    const YBCPgTypeAttrs *type_attrs, int num_elems,
    YBCPgExpr *elems, YBCPgExpr *expr_handle) {
  return ToYBCStatus(pgapi->NewTupleExpr(
      stmt, tuple_type_entity, type_attrs, num_elems, elems, expr_handle));
}

YBCStatus YBCGetDocDBKeySize(uint64_t data, const YBCPgTypeEntity *typeentity,
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


YBCStatus YBCAppendDatumToKey(uint64_t data, const YBCPgTypeEntity *typeentity,
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

YBCStatus YBCPgBeginTransaction(int64_t start_time) {
  return ToYBCStatus(pgapi->BeginTransaction(start_time));
}

YBCStatus YBCPgRecreateTransaction() {
  return ToYBCStatus(pgapi->RecreateTransaction());
}

YBCStatus YBCPgRestartTransaction() {
  return ToYBCStatus(pgapi->RestartTransaction());
}

YBCStatus YBCPgResetTransactionReadPoint() {
  return ToYBCStatus(pgapi->ResetTransactionReadPoint());
}

double YBCGetTransactionPriority() {
  return pgapi->GetTransactionPriority();
}

TxnPriorityRequirement YBCGetTransactionPriorityType() {
  return pgapi->GetTransactionPriorityType();
}

YBCStatus YBCPgEnsureReadPoint() {
  return ToYBCStatus(pgapi->EnsureReadPoint());
}

YBCStatus YBCPgRestartReadPoint() {
  return ToYBCStatus(pgapi->RestartReadPoint());
}

bool YBCIsRestartReadPointRequested() {
  return pgapi->IsRestartReadPointRequested();
}

YBCStatus YBCPgCommitPlainTransaction() {
  return ToYBCStatus(pgapi->CommitPlainTransaction());
}

YBCStatus YBCPgAbortPlainTransaction() {
  return ToYBCStatus(pgapi->AbortPlainTransaction());
}

YBCStatus YBCPgSetTransactionIsolationLevel(int isolation) {
  return ToYBCStatus(pgapi->SetTransactionIsolationLevel(isolation));
}

YBCStatus YBCPgSetTransactionReadOnly(bool read_only) {
  return ToYBCStatus(pgapi->SetTransactionReadOnly(read_only));
}

YBCStatus YBCPgUpdateFollowerReadsConfig(bool enable_follower_reads, int32_t staleness_ms) {
  return ToYBCStatus(pgapi->UpdateFollowerReadsConfig(enable_follower_reads, staleness_ms));
}

YBCStatus YBCPgSetEnableTracing(bool tracing) {
  return ToYBCStatus(pgapi->SetEnableTracing(tracing));
}

YBCStatus YBCPgSetTransactionDeferrable(bool deferrable) {
  return ToYBCStatus(pgapi->SetTransactionDeferrable(deferrable));
}

YBCStatus YBCPgSetInTxnBlock(bool in_txn_blk) {
  return ToYBCStatus(pgapi->SetInTxnBlock(in_txn_blk));
}

YBCStatus YBCPgSetReadOnlyStmt(bool read_only_stmt) {
  return ToYBCStatus(pgapi->SetReadOnlyStmt(read_only_stmt));
}

YBCStatus YBCPgEnterSeparateDdlTxnMode() {
  return ToYBCStatus(pgapi->EnterSeparateDdlTxnMode());
}

bool YBCPgHasWriteOperationsInDdlTxnMode() {
  return pgapi->HasWriteOperationsInDdlTxnMode();
}

YBCStatus YBCPgExitSeparateDdlTxnMode(YBCPgOid db_oid, bool is_silent_altering) {
  return ToYBCStatus(pgapi->ExitSeparateDdlTxnMode(db_oid, is_silent_altering));
}

YBCStatus YBCPgClearSeparateDdlTxnMode() {
  return ToYBCStatus(pgapi->ClearSeparateDdlTxnMode());
}

YBCStatus YBCPgSetActiveSubTransaction(uint32_t id) {
  return ToYBCStatus(pgapi->SetActiveSubTransaction(id));
}

YBCStatus YBCPgRollbackToSubTransaction(uint32_t id) {
  return ToYBCStatus(pgapi->RollbackToSubTransaction(id));
}

YBCStatus YBCPgGetSelfActiveTransaction(YBCPgUuid *txn_id, bool *is_null) {
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

YBCStatus YBCPgActiveTransactions(YBCPgSessionTxnInfo *infos, size_t num_infos) {
  return ToYBCStatus(pgapi->GetActiveTransactions(infos, num_infos));
}

bool YBCPgIsDdlMode() {
  return pgapi->IsDdlMode();
}

//------------------------------------------------------------------------------------------------
// System validation.
//------------------------------------------------------------------------------------------------
YBCStatus YBCPgValidatePlacement(const char *placement_info) {
  return ToYBCStatus(pgapi->ValidatePlacement(placement_info));
}

// Referential Integrity Caching
YBCStatus YBCPgForeignKeyReferenceCacheDelete(const YBCPgYBTupleIdDescriptor *source) {
  return ProcessYbctid(*source, [](auto table_id, const auto& ybctid){
    pgapi->DeleteForeignKeyReference(table_id, ybctid);
    return Status::OK();
  });
}

void YBCPgDeleteFromForeignKeyReferenceCache(YBCPgOid table_relfilenode_oid, uint64_t ybctid) {
  pgapi->DeleteForeignKeyReference(table_relfilenode_oid, YbctidAsSlice(ybctid));
}

void YBCPgAddIntoForeignKeyReferenceCache(YBCPgOid table_relfilenode_oid, uint64_t ybctid) {
  pgapi->AddForeignKeyReference(table_relfilenode_oid, YbctidAsSlice(ybctid));
}

YBCStatus YBCForeignKeyReferenceExists(const YBCPgYBTupleIdDescriptor *source, bool* res) {
  return ProcessYbctid(*source, [res, source](auto table_id, const auto& ybctid) -> Status {
    *res = VERIFY_RESULT(pgapi->ForeignKeyReferenceExists(table_id, ybctid, source->database_oid));
    return Status::OK();
  });
}

YBCStatus YBCAddForeignKeyReferenceIntent(
    const YBCPgYBTupleIdDescriptor *source, bool relation_is_region_local) {
  return ProcessYbctid(*source, [relation_is_region_local](auto table_id, const auto& ybctid) {
    pgapi->AddForeignKeyReferenceIntent(table_id, relation_is_region_local, ybctid);
    return Status::OK();
  });
}

YBCPgExplicitRowLockStatus YBCAddExplicitRowLockIntent(
    YBCPgOid table_relfilenode_oid, uint64_t ybctid, YBCPgOid database_oid,
    const PgExplicitRowLockParams *params, bool is_region_local) {
  auto result = MakePgExplicitRowLockStatus();
  result.ybc_status = ToYBCStatus(pgapi->AddExplicitRowLockIntent(
      PgObjectId(database_oid, table_relfilenode_oid), YbctidAsSlice(ybctid), *params,
      is_region_local, result.error_info));
  return result;
}

YBCPgExplicitRowLockStatus YBCFlushExplicitRowLockIntents() {
  auto result = MakePgExplicitRowLockStatus();
  result.ybc_status = ToYBCStatus(pgapi->FlushExplicitRowLockIntents(result.error_info));
  return result;
}

// INSERT ... ON CONFLICT batching -----------------------------------------------------------------
YBCStatus YBCPgAddInsertOnConflictKey(const YBCPgYBTupleIdDescriptor* tupleid, void* state,
                                      YBCPgInsertOnConflictKeyInfo* info) {
  return ProcessYbctid(*tupleid, [state, info](auto table_id, const auto& ybctid) {
    return pgapi->AddInsertOnConflictKey(
        table_id, ybctid, state, info ? *info : YBCPgInsertOnConflictKeyInfo());
  });
}

YBCStatus YBCPgInsertOnConflictKeyExists(const YBCPgYBTupleIdDescriptor* tupleid,
                                         void* state,
                                         YBCPgInsertOnConflictKeyState* res) {
  return ProcessYbctid(*tupleid, [res, state](auto table_id, const auto& ybctid) {
    *res = pgapi->InsertOnConflictKeyExists(table_id, ybctid, state);
    return Status::OK();
  });
}

YBCStatus YBCPgDeleteInsertOnConflictKey(const YBCPgYBTupleIdDescriptor* tupleid,
                                         void* state,
                                         YBCPgInsertOnConflictKeyInfo* info) {
  return ProcessYbctid(*tupleid, [state, info](auto table_id, const auto& ybctid) {
    auto result = VERIFY_RESULT(pgapi->DeleteInsertOnConflictKey(table_id, ybctid, state));
    *info = result;
    return (Status) Status::OK();
  });
}

YBCStatus YBCPgDeleteNextInsertOnConflictKey(void* state, YBCPgInsertOnConflictKeyInfo* info) {
  return ExtractValueFromResult(pgapi->DeleteNextInsertOnConflictKey(state), info);
}

YBCStatus YBCPgAddInsertOnConflictKeyIntent(const YBCPgYBTupleIdDescriptor* tupleid) {
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

bool YBCIsInitDbModeEnvVarSet() {
  static bool cached_value = false;
  static bool cached = false;

  if (!cached) {
    const char* initdb_mode_env_var_value = getenv("YB_PG_INITDB_MODE");
    cached_value = initdb_mode_env_var_value && strcmp(initdb_mode_env_var_value, "1") == 0;
    cached = true;
  }

  return cached_value;
}

void YBCInitFlags() {
  SetAtomicFlag(GetAtomicFlag(&FLAGS_pggate_num_connections_to_server),
                &FLAGS_num_connections_to_server);

  // TODO(neil) Init a gflag for "YB_PG_TRANSACTIONS_ENABLED" here also.
  // Mikhail agreed that this flag should just be initialized once at the beginning here.
  // Currently, it is initialized for every CREATE statement.
}

YBCStatus YBCPgIsInitDbDone(bool* initdb_done) {
  return ExtractValueFromResult(pgapi->IsInitDbDone(), initdb_done);
}

bool YBCGetDisableTransparentCacheRefreshRetry() {
  return pgapi->GetDisableTransparentCacheRefreshRetry();
}

YBCStatus YBCGetSharedCatalogVersion(uint64_t* catalog_version) {
  return ExtractValueFromResult(pgapi->GetSharedCatalogVersion(), catalog_version);
}

YBCStatus YBCGetSharedDBCatalogVersion(YBCPgOid db_oid, uint64_t* catalog_version) {
  return ExtractValueFromResult(pgapi->GetSharedCatalogVersion(db_oid), catalog_version);
}

YBCStatus YBCGetNumberOfDatabases(uint32_t* num_databases) {
  return ExtractValueFromResult(pgapi->GetNumberOfDatabases(), num_databases);
}

YBCStatus YBCCatalogVersionTableInPerdbMode(bool* perdb_mode) {
  return ExtractValueFromResult(pgapi->CatalogVersionTableInPerdbMode(), perdb_mode);
}

uint64_t YBCGetSharedAuthKey() {
  return pgapi->GetSharedAuthKey();
}

const unsigned char* YBCGetLocalTserverUuid() {
  return pgapi->GetLocalTserverUuid();
}

const YBCPgGFlagsAccessor* YBCGetGFlags() {
  // clang-format off
  static YBCPgGFlagsAccessor accessor = {
      .log_ysql_catalog_versions                = &FLAGS_log_ysql_catalog_versions,
      .ysql_catalog_preload_additional_tables   = &FLAGS_ysql_catalog_preload_additional_tables,
      .ysql_disable_index_backfill              = &FLAGS_ysql_disable_index_backfill,
      .ysql_disable_server_file_access          = &FLAGS_ysql_disable_server_file_access,
      .ysql_enable_reindex                      = &FLAGS_ysql_enable_reindex,
      .ysql_num_databases_reserved_in_db_catalog_version_mode =
          &FLAGS_ysql_num_databases_reserved_in_db_catalog_version_mode,
      .ysql_output_buffer_size                  = &FLAGS_ysql_output_buffer_size,
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
      .TEST_ysql_hide_catalog_version_increment_log =
          &FLAGS_TEST_ysql_hide_catalog_version_increment_log,
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
  };
  // clang-format on
  return &accessor;
}

bool YBCPgIsYugaByteEnabled() {
  return pgapi;
}

void YBCSetTimeout(int timeout_ms, void* extra) {
  if (!pgapi) {
    return;
  }
  const auto default_client_timeout_ms = client::YsqlClientReadWriteTimeoutMs();
  // We set the rpc timeouts as a min{STATEMENT_TIMEOUT,
  // FLAGS(_ysql)?_client_read_write_timeout_ms}.
  // Note that 0 is a valid value of timeout_ms, meaning no timeout in Postgres.
  if (timeout_ms < 0) {
    // The timeout is not valid. Use the default GFLAG value.
    return;
  } else if (timeout_ms == 0) {
    timeout_ms = default_client_timeout_ms;
  } else {
    timeout_ms = std::min(timeout_ms, default_client_timeout_ms);
  }

  // The statement timeout is lesser than default_client_timeout, hence the rpcs would
  // need to use a shorter timeout.
  pgapi->SetTimeout(timeout_ms);
}

YBCStatus YBCNewGetLockStatusDataSRF(YBCPgFunction *handle) {
  return ToYBCStatus(pgapi->NewGetLockStatusDataSRF(handle));
}

YBCStatus YBCGetTabletServerHosts(YBCServerDescriptor **servers, size_t *count) {
  const auto result = pgapi->ListTabletServers();
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }
  const auto &servers_info = result.get();
  *count = servers_info.size();
  *servers = NULL;
  if (!servers_info.empty()) {
    *servers = static_cast<YBCServerDescriptor *>(
        YBCPAlloc(sizeof(YBCServerDescriptor) * servers_info.size()));
    YBCServerDescriptor *dest = *servers;
    for (const auto &info : servers_info) {
      new (dest) YBCServerDescriptor {
        .host = YBCPAllocStdString(info.server.hostname),
        .cloud = YBCPAllocStdString(info.cloud),
        .region = YBCPAllocStdString(info.region),
        .zone = YBCPAllocStdString(info.zone),
        .public_ip = YBCPAllocStdString(info.public_ip),
        .is_primary = info.is_primary,
        .pg_port = info.pg_port,
        .uuid = YBCPAllocStdString(info.server.uuid),
      };
      ++dest;
    }
  }
  return YBCStatusOK();
}

YBCStatus YBCGetIndexBackfillProgress(YBCPgOid* index_oids, YBCPgOid* database_oids,
                                      uint64_t** backfill_statuses,
                                      int num_indexes) {
  std::vector<PgObjectId> index_ids;
  for (int i = 0; i < num_indexes; ++i) {
    index_ids.emplace_back(PgObjectId(database_oids[i], index_oids[i]));
  }
  return ToYBCStatus(pgapi->GetIndexBackfillProgress(index_ids, backfill_statuses));
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

YBCPgThreadLocalRegexpCache* YBCPgGetThreadLocalRegexpCache() {
  return PgGetThreadLocalRegexpCache();
}

YBCPgThreadLocalRegexpCache* YBCPgInitThreadLocalRegexpCache(
    size_t buffer_size, YBCPgThreadLocalRegexpCacheCleanup cleanup) {
  return PgInitThreadLocalRegexpCache(buffer_size, cleanup);
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
    YBCPgOid database_oid,
    YBCPgLastKnownCatalogVersionInfo version_info,
    YBCPgSysTablePrefetcherCacheMode cache_mode) {
  YBCStartSysTablePrefetchingImpl(PrefetcherOptions::CachingInfo{
      {version_info.version, version_info.is_db_catalog_version_mode},
      database_oid,
      YBCMapPrefetcherCacheMode(cache_mode)});
}

void YBCStopSysTablePrefetching() {
  pgapi->StopSysTablePrefetching();
}

bool YBCIsSysTablePrefetchingStarted() {
  return pgapi->IsSysTablePrefetchingStarted();
}

void YBCRegisterSysTableForPrefetching(
    YBCPgOid database_oid, YBCPgOid table_oid, YBCPgOid index_oid, int row_oid_filtering_attr,
    bool fetch_ybctid) {
  pgapi->RegisterSysTableForPrefetching(
      PgObjectId(database_oid, table_oid), PgObjectId(database_oid, index_oid),
      row_oid_filtering_attr, fetch_ybctid);
}

YBCStatus YBCPrefetchRegisteredSysTables() {
  return ToYBCStatus(pgapi->PrefetchRegisteredSysTables());
}

YBCStatus YBCPgCheckIfPitrActive(bool* is_active) {
  auto res = pgapi->CheckIfPitrActive();
  if (res.ok()) {
    *is_active = *res;
    return YBCStatusOK();
  }
  return ToYBCStatus(res.status());
}

YBCStatus YBCIsObjectPartOfXRepl(YBCPgOid database_oid, YBCPgOid table_relfilenode_oid,
    bool* is_object_part_of_xrepl) {
  auto res = pgapi->IsObjectPartOfXRepl(PgObjectId(database_oid, table_relfilenode_oid));
  if (res.ok()) {
    *is_object_part_of_xrepl = *res;
    return YBCStatusOK();
  }
  return ToYBCStatus(res.status());
}

YBCStatus YBCPgCancelTransaction(const unsigned char* transaction_id) {
  return ToYBCStatus(pgapi->CancelTransaction(transaction_id));
}

YBCStatus YBCGetTableKeyRanges(
    YBCPgOid database_oid, YBCPgOid table_relfilenode_oid, const char* lower_bound_key,
    size_t lower_bound_key_size, const char* upper_bound_key, size_t upper_bound_key_size,
    uint64_t max_num_ranges, uint64_t range_size_bytes, bool is_forward, uint32_t max_key_length,
    YBCGetTableKeyRangesCallback callback, void* callback_param) {
  return ToYBCStatus(YBCGetTableKeyRangesImpl(
      PgObjectId(database_oid, table_relfilenode_oid), Slice(lower_bound_key, lower_bound_key_size),
      Slice(upper_bound_key, upper_bound_key_size), max_num_ranges, range_size_bytes, is_forward,
      max_key_length, callback, callback_param));
}

//--------------------------------------------------------------------------------------------------
// Replication Slots.

YBCStatus YBCPgNewCreateReplicationSlot(const char *slot_name,
                                        const char *plugin_name,
                                        YBCPgOid database_oid,
                                        YBCPgReplicationSlotSnapshotAction snapshot_action,
                                        YBCLsnType lsn_type,
                                        YBCPgStatement *handle) {
  return ToYBCStatus(pgapi->NewCreateReplicationSlot(slot_name,
                                                     plugin_name,
                                                     database_oid,
                                                     snapshot_action,
                                                     lsn_type,
                                                     handle));
}

YBCStatus YBCPgExecCreateReplicationSlot(YBCPgStatement handle,
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

YBCStatus YBCPgListReplicationSlots(
    YBCReplicationSlotDescriptor **replication_slots, size_t *numreplicationslots) {
  const auto result = pgapi->ListReplicationSlots();
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }
  VLOG(4) << "The ListReplicationSlots response: " << result->DebugString();

  const auto &replication_slots_info = result.get().replication_slots();
  *DCHECK_NOTNULL(numreplicationslots) = replication_slots_info.size();
  *DCHECK_NOTNULL(replication_slots) = NULL;
  if (!replication_slots_info.empty()) {
    *replication_slots = static_cast<YBCReplicationSlotDescriptor *>(
        YBCPAlloc(sizeof(YBCReplicationSlotDescriptor) * replication_slots_info.size()));
    YBCReplicationSlotDescriptor *dest = *replication_slots;
    for (const auto &info : replication_slots_info) {
      int replica_identities_count = info.replica_identity_map_size();
      YBCPgReplicaIdentityDescriptor* replica_identities =
          static_cast<YBCPgReplicaIdentityDescriptor*>(
              YBCPAlloc(sizeof(YBCPgReplicaIdentityDescriptor) * replica_identities_count));

      int replica_identity_idx = 0;
      for (const auto& replica_identity : info.replica_identity_map()) {
        replica_identities[replica_identity_idx].table_oid = replica_identity.first;
        replica_identities[replica_identity_idx].identity_type =
            GetReplicaIdentity(replica_identity.second);
        replica_identity_idx++;
      }

      new (dest) YBCReplicationSlotDescriptor{
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
          .last_pub_refresh_time = info.last_pub_refresh_time()
      };
      ++dest;
    }
  }
  return YBCStatusOK();
}

YBCStatus YBCPgGetReplicationSlot(
    const char *slot_name, YBCReplicationSlotDescriptor **replication_slot) {
  const auto replication_slot_name = ReplicationSlotName(std::string(slot_name));
  const auto result = pgapi->GetReplicationSlot(replication_slot_name);
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }

  VLOG(4) << "The GetReplicationSlot for slot_name = " << std::string(slot_name)
          << " is: " << result->DebugString();

  const auto& slot_info = result.get().replication_slot_info();

  *replication_slot =
      static_cast<YBCReplicationSlotDescriptor*>(YBCPAlloc(sizeof(YBCReplicationSlotDescriptor)));

  int replica_identities_count = slot_info.replica_identity_map_size();
  YBCPgReplicaIdentityDescriptor* replica_identities = static_cast<YBCPgReplicaIdentityDescriptor*>(
      YBCPAlloc(sizeof(YBCPgReplicaIdentityDescriptor) * replica_identities_count));

  int replica_identity_idx = 0;
  for (const auto& replica_identity : slot_info.replica_identity_map()) {
    replica_identities[replica_identity_idx].table_oid = replica_identity.first;
    replica_identities[replica_identity_idx].identity_type =
        GetReplicaIdentity(replica_identity.second);
    replica_identity_idx++;
  }

  new (*replication_slot) YBCReplicationSlotDescriptor{
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
      .last_pub_refresh_time = slot_info.last_pub_refresh_time()
  };

  return YBCStatusOK();
}

YBCStatus YBCPgNewDropReplicationSlot(const char *slot_name,
                                      YBCPgStatement *handle) {
  return ToYBCStatus(pgapi->NewDropReplicationSlot(slot_name,
                                                   handle));
}

YBCStatus YBCPgExecDropReplicationSlot(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecDropReplicationSlot(handle));
}

YBCStatus YBCYcqlStatementStats(YCQLStatementStats** stats, size_t* num_stats) {
  const auto result = pgapi->YCQLStatementStats();
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }
  const auto& statements_stat = result->statements();
  *num_stats = statements_stat.size();
  *stats = NULL;
  if (!statements_stat.empty()) {
    *stats = static_cast<YCQLStatementStats*>(
        YBCPAlloc(sizeof(YCQLStatementStats) * statements_stat.size()));
    YCQLStatementStats *dest = *stats;
    for (const auto &info : statements_stat) {
      new (dest) YCQLStatementStats {
          .queryid = info.queryid(),
          .query = YBCPAllocStdString(info.query()),
          .is_prepared = info.is_prepared(),
          .calls = info.calls(),
          .total_time = info.total_time(),
          .min_time = info.min_time(),
          .max_time = info.max_time(),
          .mean_time = info.mean_time(),
          .stddev_time = info.stddev_time(),
      };
      ++dest;
    }
  }
  return YBCStatusOK();
}

void YBCStoreTServerAshSamples(
    YBCAshAcquireBufferLock acquire_cb_lock_fn, YBCAshGetNextCircularBufferSlot get_cb_slot_fn,
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

YBCStatus YBCPgInitVirtualWalForCDC(
    const char *stream_id, const YBCPgOid database_oid, YBCPgOid *relations, YBCPgOid *relfilenodes,
    size_t num_relations) {
  std::vector<PgObjectId> tables;
  tables.reserve(num_relations);

  for (size_t i = 0; i < num_relations; i++) {
    PgObjectId table_id(database_oid, relfilenodes[i]);
    tables.push_back(std::move(table_id));
  }

  const auto result = pgapi->InitVirtualWALForCDC(std::string(stream_id), tables);
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }

  return YBCStatusOK();
}

YBCStatus YBCPgUpdatePublicationTableList(
    const char* stream_id, const YBCPgOid database_oid, YBCPgOid* relations, YBCPgOid* relfilenodes,
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

YBCStatus YBCPgDestroyVirtualWalForCDC() {
  const auto result = pgapi->DestroyVirtualWALForCDC();
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }

  return YBCStatusOK();
}

YBCPgRowMessageAction GetRowMessageAction(yb::cdc::RowMessage row_message_pb) {
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

YBCStatus YBCPgGetCDCConsistentChanges(
    const char* stream_id,
    YBCPgChangeRecordBatch** record_batch,
    YBCTypeEntityProvider type_entity_provider) {
  const auto result = pgapi->GetConsistentChangesForCDC(std::string(stream_id));
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }

  *DCHECK_NOTNULL(record_batch) = NULL;
  const auto resp = result.get();
  VLOG(4) << "The GetConsistentChangesForCDC response: " << resp.DebugString();
  auto response_to_pg_conversion_start = GetCurrentTimeMicros();
  auto row_count = resp.cdc_sdk_proto_records_size();

  // Used for logging a summary of the response received from the CDC service.
  YBCPgXLogRecPtr min_resp_lsn = 0xFFFFFFFFFFFFFFFF;
  YBCPgXLogRecPtr max_resp_lsn = 0;
  uint32_t min_txn_id = 0xFFFFFFFF;
  uint32_t max_txn_id = 0;

  auto resp_rows_pb = resp.cdc_sdk_proto_records();
  auto resp_rows = static_cast<YBCPgRowMessage *>(YBCPAlloc(sizeof(YBCPgRowMessage) * row_count));
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
      YBCPgTableDesc tableDesc = nullptr;
      auto s = pgapi->GetTableDesc(table_id, &tableDesc);
      if (!s.ok()) {
        return ToYBCStatus(s);
      }
      table_oid = tableDesc->pg_table_id().object_oid;
    }

    auto col_count = narrow_cast<int>(col_name_idx_map.size());
    YBCPgDatumMessage *cols = nullptr;
    if (col_count > 0) {
      cols = static_cast<YBCPgDatumMessage *>(YBCPAlloc(sizeof(YBCPgDatumMessage) * col_count));

      int tuple_idx = 0;
      for (const auto& col_idxs : col_name_idx_map) {
        YBCPgTypeAttrs type_attrs{-1 /* typmod */};
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
              static_cast<int>(old_datum_pb->column_type()), old_datum_pb->col_attr_num(),
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
              static_cast<int>(new_datum_pb->column_type()), new_datum_pb->col_attr_num(),
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


    new (&resp_rows[row_idx]) YBCPgRowMessage{
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

  *record_batch = static_cast<YBCPgChangeRecordBatch *>(YBCPAlloc(sizeof(YBCPgChangeRecordBatch)));
  new (*record_batch) YBCPgChangeRecordBatch{
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

YBCStatus YBCPgUpdateAndPersistLSN(
    const char* stream_id, YBCPgXLogRecPtr restart_lsn_hint, YBCPgXLogRecPtr confirmed_flush,
    YBCPgXLogRecPtr* restart_lsn) {
  const auto result =
      pgapi->UpdateAndPersistLSN(std::string(stream_id), restart_lsn_hint, confirmed_flush);
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }

  *DCHECK_NOTNULL(restart_lsn) = result->restart_lsn();
  return YBCStatusOK();
}

YBCStatus YBCLocalTablets(YBCPgTabletsDescriptor** tablets, size_t* count) {
  const auto result = pgapi->TabletsMetadata();
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }
  const auto& local_tablets = result.get().tablets();
  *count = local_tablets.size();
  if (!local_tablets.empty()) {
    *tablets = static_cast<YBCPgTabletsDescriptor*>(
        YBCPAlloc(sizeof(YBCPgTabletsDescriptor) * local_tablets.size()));
    YBCPgTabletsDescriptor* dest = *tablets;
    for (const auto& tablet : local_tablets) {
      new (dest) YBCPgTabletsDescriptor {
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

YBCStatus YBCServersMetrics(YBCPgServerMetricsInfo** servers_metrics_info, size_t* count) {
  const auto result = pgapi->ServersMetrics();
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }
  const auto& servers_metrics = result.get().servers_metrics();
  *count = servers_metrics.size();
  if (!servers_metrics.empty()) {
    *servers_metrics_info = static_cast<YBCPgServerMetricsInfo*>(
        YBCPAlloc(sizeof(YBCPgServerMetricsInfo) * servers_metrics.size()));
    YBCPgServerMetricsInfo* dest = *servers_metrics_info;
    for (const auto& server_metrics_info : servers_metrics) {
      size_t metrics_count = server_metrics_info.metrics().size();
      YBCMetricsInfo* metrics =
          static_cast<YBCMetricsInfo*>(
              YBCPAlloc(sizeof(YBCMetricsInfo) * metrics_count));

      int metrics_idx = 0;
      for (const auto& metrics_info : server_metrics_info.metrics()) {
        metrics[metrics_idx].name = YBCPAllocStdString(metrics_info.name());
        metrics[metrics_idx].value = YBCPAllocStdString(metrics_info.value());
        metrics_idx++;
      }
      new (dest) YBCPgServerMetricsInfo {
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

YBCStatus YBCDatabaseClones(YBCPgDatabaseCloneInfo** database_clones, size_t* count) {
  const auto result = pgapi->GetDatabaseClones();
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }
  const auto& tserver_clone_entries = result.get().database_clones();
  *count = tserver_clone_entries.size();
  if (!tserver_clone_entries.empty()) {
    *database_clones = static_cast<YBCPgDatabaseCloneInfo*>(
        YBCPAlloc(sizeof(YBCPgDatabaseCloneInfo) * tserver_clone_entries.size()));
    YBCPgDatabaseCloneInfo* cur_clone = *database_clones;
    for (const auto& tserver_clone_entry : tserver_clone_entries) {
      new (cur_clone) YBCPgDatabaseCloneInfo{
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

YBCStatus YBCSetCronLastMinute(int64_t last_minute) {
  return ToYBCStatus(pgapi->SetCronLastMinute(last_minute));
}

YBCStatus YBCGetCronLastMinute(int64_t* last_minute) {
  const auto result = pgapi->GetCronLastMinute();
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }
  *last_minute = result.get();

  return YBCStatusOK();
}

uint64_t YBCPgGetCurrentReadTimePoint() {
  return pgapi->GetCurrentReadTimePoint();
}

YBCStatus YBCRestoreReadTimePoint(uint64_t read_time_point_handle) {
  return ToYBCStatus(pgapi->RestoreReadTimePoint(read_time_point_handle));
}

void YBCForceAllowCatalogModifications(bool allowed) {
  pgapi->ForceAllowCatalogModifications(allowed);
}

uint64_t YBCGetCurrentHybridTimeLsn() {
  return (HybridTime::FromMicros(GetCurrentTimeMicros()).ToUint64());
}

YBCStatus YBCAcquireAdvisoryLock(
    YBAdvisoryLockId lock_id, YBAdvisoryLockMode mode, bool wait, bool session) {
  return ToYBCStatus(pgapi->AcquireAdvisoryLock(lock_id, mode, wait, session));
}

YBCStatus YBCReleaseAdvisoryLock(YBAdvisoryLockId lock_id, YBAdvisoryLockMode mode) {
  return ToYBCStatus(pgapi->ReleaseAdvisoryLock(lock_id, mode));
}

YBCStatus YBCReleaseAllAdvisoryLocks(uint32_t db_oid) {
  return ToYBCStatus(pgapi->ReleaseAllAdvisoryLocks(db_oid));
}

} // extern "C"

} // namespace yb::pggate
