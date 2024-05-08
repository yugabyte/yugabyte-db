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

#include "yb/yql/pggate/ybc_pggate.h"

#include <algorithm>
#include <atomic>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

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

#include "yb/server/skewed_clock.h"

#include "yb/util/atomic.h"
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

DECLARE_bool(ysql_ddl_rollback_enabled);

DEFINE_UNKNOWN_bool(ysql_enable_reindex, false,
            "Enable REINDEX INDEX statement.");
TAG_FLAG(ysql_enable_reindex, advanced);
TAG_FLAG(ysql_enable_reindex, hidden);

DEFINE_UNKNOWN_bool(ysql_disable_server_file_access, false,
            "If true, disables read, write, and execute of local server files. "
            "File access can be re-enabled if set to false.");

DEFINE_NON_RUNTIME_bool(ysql_enable_profile, false, "Enable PROFILE feature.");

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

DEFINE_test_flag(bool, ash_debug_aux, false, "Set ASH aux_info to the first 16 characters"
    " of the method tserver is running");

namespace {

bool PreloadAdditionalCatalogListValidator(const char* flag_name, const std::string& flag_val) {
  for (const char& c : flag_val) {
    if (c != '_' && c != ',' && !islower(c)) {
      LOG(ERROR) << "Found invalid character " << c << " in flag " << flag_name;
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

inline std::optional<Bound> MakeBound(YBCPgBoundType type, uint64_t value) {
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
    YBCInitPgGateEx(data_type_table, count, pg_callbacks, nullptr /* context */, opt_session_id,
                    ash_config);
    return static_cast<Status>(Status::OK());
  });
}

Status PgInitSessionImpl(const char* database_name, YBCPgExecStatsState* session_stats) {
  const std::string db_name(database_name ? database_name : "");
  return WithMaskedYsqlSignals(
      [&db_name, session_stats] { return pgapi->InitSession(db_name, session_stats); });
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
                      bool *has_null) {
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

// Sets the client address in the ASH circular buffer and returns the address family.
// The host comes from the class HostPort and that only accepts IPv4/IPv6 as of today,
// that's why there is no check for a unix domain socket here.
uint8_t AshGetTServerClientAddress(const std::string& host, unsigned char* client_addr) {
  if (host.find(':') == std::string::npos) { // IPv4 address
    inet_pton(AF_INET, host.c_str(), client_addr);
    return AF_INET;
  }
  // IPv6 address
  inet_pton(AF_INET6, host.c_str(), client_addr);
  return AF_INET6;
}

uint32_t AshEncodeWaitStateCodeWithComponent(uint32_t component, uint32_t code) {
  DCHECK_EQ((component >> YB_ASH_COMPONENT_BITS), 0);
  DCHECK_EQ((code >> YB_ASH_COMPONENT_POSITION), 0);
  return (component << YB_ASH_COMPONENT_POSITION) | code;
}

void AshCopyTServerSample(
    YBCAshSample* cb_sample, uint32_t component, const WaitStateInfoPB& tserver_sample,
    uint64_t sample_time) {
  auto* cb_metadata = &cb_sample->metadata;
  const auto& tserver_metadata = tserver_sample.metadata();

  cb_metadata->query_id = tserver_metadata.query_id();
  cb_metadata->session_id = tserver_metadata.session_id();
  cb_metadata->client_port =  static_cast<uint16_t>(tserver_metadata.client_host_port().port());
  cb_sample->rpc_request_id = tserver_metadata.rpc_request_id();
  cb_sample->encoded_wait_event_code =
      AshEncodeWaitStateCodeWithComponent(component, tserver_sample.wait_status_code());
  cb_sample->sample_weight = 1; // TODO: Change this once sampling is done at tserver side
  cb_sample->sample_time = sample_time;

  std::memcpy(cb_metadata->root_request_id,
              tserver_metadata.root_request_id().data(),
              sizeof(cb_metadata->root_request_id));

  std::memcpy(cb_sample->yql_endpoint_tserver_uuid,
              tserver_metadata.yql_endpoint_tserver_uuid().data(),
              sizeof(cb_sample->yql_endpoint_tserver_uuid));

  // Copy the entire aux info, or the first 15 bytes, whichever is smaller.
  // This expects compilation with -Wno-format-truncation.
  const auto& tserver_aux_info = tserver_sample.aux_info();
  snprintf(cb_sample->aux_info, sizeof(cb_sample->aux_info), "%s",
      FLAGS_TEST_ash_debug_aux ? tserver_aux_info.method().c_str()
                               : tserver_aux_info.tablet_id().c_str());

  cb_metadata->addr_family = AshGetTServerClientAddress(
      tserver_metadata.client_host_port().host(), cb_metadata->client_addr);
}

void AshCopyTServerSamples(
    YBCAshGetNextCircularBufferSlot get_cb_slot_fn, const tserver::WaitStatesPB& samples,
    uint64_t sample_time) {
  for (const auto& sample : samples.wait_states()) {
    AshCopyTServerSample(get_cb_slot_fn(), samples.component(), sample, sample_time);
  }
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

  InitThreading();

  CHECK(pgapi == nullptr) << ": " << __PRETTY_FUNCTION__ << " can only be called once";

  YBCInitFlags();

#ifndef NDEBUG
  HybridTime::TEST_SetPrettyToString(true);
#endif

  pgapi_shutdown_done.exchange(false);
  if (context) {
    pgapi = new pggate::PgApiImpl(
      std::move(*context), data_type_table, count, pg_callbacks, session_id, ash_config);
  } else {
    pgapi = new pggate::PgApiImpl(PgApiContext(), data_type_table, count, pg_callbacks, session_id,
                                  ash_config);
  }

  VLOG(1) << "PgGate open";
}

extern "C" {

void YBCInitPgGate(const YBCPgTypeEntity *data_type_table, int count, PgCallbacks pg_callbacks,
                   uint64_t *session_id, const YBCPgAshConfig* ash_config) {
  CHECK_OK(InitPgGateImpl(data_type_table, count, pg_callbacks, session_id, ash_config));
}

void YBCDestroyPgGate() {
  LOG_IF(DFATAL, !is_main_thread())
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
  LOG_IF(DFATAL, !is_main_thread())
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

bool YBCGetCurrentPgSessionId(uint64_t *session_id) {
  if (pgapi) {
    *session_id = pgapi->GetSessionId();
    return true;
  }
  return false;
}

YBCStatus YBCPgInitSession(const char* database_name, YBCPgExecStatsState* session_stats) {
  return ToYBCStatus(PgInitSessionImpl(database_name, session_stats));
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

YBCPgDataType YBCPgGetType(const YBCPgTypeEntity *type_entity) {
  if (type_entity) {
    return type_entity->yb_type;
  }
  return YB_YQL_DATA_TYPE_UNKNOWN_DATA;
}

bool YBCPgAllowForPrimaryKey(const YBCPgTypeEntity *type_entity) {
  if (type_entity) {
    return type_entity->allow_for_primary_key;
  }
  return false;
}

YBCStatus YBCGetPgggateCurrentAllocatedBytes(int64_t *consumption) {
  *consumption = GetTCMallocCurrentAllocatedBytes();
  return YBCStatusOK();
}

YBCStatus YbGetActualHeapSizeBytes(int64_t *consumption) {
#ifdef YB_TCMALLOC_ENABLED
    // Use GetRootMemTrackerConsumption instead of directly accessing TCMalloc to avoid excess
    // calls to TCMalloc on every memory allocation.
    *consumption = pgapi ? pgapi->GetRootMemTrackerConsumption() : 0;
#else
    *consumption = 0;
#endif
    return YBCStatusOK();
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
      new_bytes += slice.size();
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
      FreeSlice(*itera);
      itera = a->erase(itera);
    } else {
      ++itera;
    }
  }

  // then delete everything from b (a copy already exists in a)
  YBCBitmapDeepDeleteSet(sb);

  return a;
}

size_t YBCBitmapInsertYbctidsIntoSet(SliceSet set, ConstSliceVector vec) {
  const std::vector<Slice> *v = reinterpret_cast<const std::vector<Slice> *>(vec);

  size_t bytes = 0;
  UnorderedSliceSet *s = reinterpret_cast<UnorderedSliceSet *>(set);

  for (auto ybctid : *v) {
    if (s->insert(ybctid).second) // successfully inserted
      bytes += ybctid.size();
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

YBCStatus YBCPgConnectDatabase(const char *database_name) {
  return ToYBCStatus(pgapi->ConnectDatabase(database_name));
}

YBCStatus YBCPgIsDatabaseColocated(const YBCPgOid database_oid, bool *colocated,
                                   bool *legacy_colocated_database) {
  return ToYBCStatus(pgapi->IsDatabaseColocated(database_oid, colocated,
                                                legacy_colocated_database));
}

YBCStatus YBCPgNewCreateDatabase(const char *database_name,
                                 const YBCPgOid database_oid,
                                 const YBCPgOid source_database_oid,
                                 const YBCPgOid next_oid,
                                 bool colocated,
                                 YBCPgStatement *handle) {
  return ToYBCStatus(pgapi->NewCreateDatabase(
      database_name, database_oid, source_database_oid, next_oid, colocated, handle));
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
                              bool if_not_exist,
                              bool add_primary_key,
                              bool is_colocated_via_database,
                              YBCPgOid tablegroup_oid,
                              YBCPgOid colocation_id,
                              YBCPgOid tablespace_oid,
                              bool is_matview,
                              YBCPgOid pg_table_oid,
                              YBCPgOid old_relfilenode_oid,
                              YBCPgStatement *handle) {
  const PgObjectId table_id(database_oid, table_relfilenode_oid);
  const PgObjectId tablegroup_id(database_oid, tablegroup_oid);
  const PgObjectId tablespace_id(database_oid, tablespace_oid);
  const PgObjectId pg_table_id(database_oid, pg_table_oid);
  const PgObjectId old_relfilenode_id(database_oid, old_relfilenode_oid);

  return ToYBCStatus(pgapi->NewCreateTable(
      database_name, schema_name, table_name, table_id, is_shared_table,
      if_not_exist, add_primary_key, is_colocated_via_database, tablegroup_id, colocation_id,
      tablespace_id, is_matview, pg_table_id, old_relfilenode_id, handle));
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

YBCStatus YBCPgExecAlterTable(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecAlterTable(handle));
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
  return ToYBCStatus(pgapi->DmlModifiesRow(handle, modifies_row));
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
                            bool *has_null) {
  return ToYBCStatus(GetSplitPoints(table_desc, type_entities, type_attrs_arr, split_datums,
                                    has_null));
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
                                           table_id, is_shared_index, is_unique_index,
                                           skip_index_backfill, if_not_exist,
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

YBCStatus YBCPgExecCreateIndex(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecCreateIndex(handle));
}

YBCStatus YBCPgNewDropIndex(const YBCPgOid database_oid,
                            const YBCPgOid index_oid,
                            bool if_exist,
                            YBCPgStatement *handle) {
  const PgObjectId index_id(database_oid, index_oid);
  return ToYBCStatus(pgapi->NewDropIndex(index_id, if_exist, handle));
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

YBCStatus YBCPgWaitForBackendsCatalogVersion(YBCPgOid dboid, uint64_t version,
                                             int* num_lagging_backends) {
  return ExtractValueFromResult(pgapi->WaitForBackendsCatalogVersion(dboid, version),
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

YBCStatus YbPgDmlAppendQual(YBCPgStatement handle, YBCPgExpr qual, bool is_primary) {
  return ToYBCStatus(pgapi->DmlAppendQual(handle, qual, is_primary));
}

YBCStatus YbPgDmlAppendColumnRef(YBCPgStatement handle, YBCPgExpr colref, bool is_primary) {
  return ToYBCStatus(pgapi->DmlAppendColumnRef(
      handle, down_cast<PgColumnRef*>(colref), is_primary));
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
  YBCPgBoundType start_type, uint64_t start_value,
  YBCPgBoundType end_type, uint64_t end_value) {
  return ToYBCStatus(pgapi->DmlBindHashCode(
      handle, MakeBound(start_type, start_value), MakeBound(end_type, end_value)));
}

YBCStatus YBCPgDmlBindRange(YBCPgStatement handle, const char *start_value, size_t start_value_len,
                            const char *end_value, size_t end_value_len) {
  return ToYBCStatus(pgapi->DmlBindRange(
    handle, Slice(start_value, start_value_len), true, Slice(end_value, end_value_len), false));
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

YBCStatus YBCPgNewSample(const YBCPgOid database_oid,
                         const YBCPgOid table_relfilenode_oid,
                         int targrows,
                         bool is_region_local,
                         YBCPgStatement *handle) {
  const PgObjectId table_id(database_oid, table_relfilenode_oid);
  return ToYBCStatus(pgapi->NewSample(table_id, targrows, is_region_local, handle));
}

YBCStatus YBCPgInitRandomState(
    YBCPgStatement handle, double rstate_w, uint64_t rand_state_s0, uint64_t rand_state_s1) {
  return ToYBCStatus(pgapi->InitRandomState(handle, rstate_w, rand_state_s0, rand_state_s1));
}

YBCStatus YBCPgSampleNextBlock(YBCPgStatement handle, bool *has_more) {
  return ToYBCStatus(pgapi->SampleNextBlock(handle, has_more));
}

YBCStatus YBCPgExecSample(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecSample(handle));
}

YBCStatus YBCPgGetEstimatedRowCount(YBCPgStatement handle, double *liverows, double *deadrows) {
  return ToYBCStatus(pgapi->GetEstimatedRowCount(handle, liverows, deadrows));
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
YBCStatus YBCPgNewSelect(const YBCPgOid database_oid,
                         const YBCPgOid table_relfilenode_oid,
                         const YBCPgPrepareParameters *prepare_params,
                         bool is_region_local,
                         YBCPgStatement *handle) {
  const PgObjectId table_id(database_oid, table_relfilenode_oid);
  const PgObjectId index_id(database_oid,
                            prepare_params ? prepare_params->index_relfilenode_oid : kInvalidOid);
  return ToYBCStatus(pgapi->NewSelect(table_id, index_id, prepare_params, is_region_local, handle));
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
  return ToYBCStatus(pgapi->RetrieveYbctids(handle, exec_params, natts, ybctids, count,
                                            exceeded_work_mem));
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

YBCStatus YBCPgRestartReadPoint() {
  return ToYBCStatus(pgapi->RestartReadPoint());
}

bool YBCIsRestartReadPointRequested() {
  return pgapi->IsRestartReadPointRequested();
}

YBCStatus YBCPgCommitTransaction() {
  return ToYBCStatus(pgapi->CommitTransaction());
}

YBCStatus YBCPgAbortTransaction() {
  return ToYBCStatus(pgapi->AbortTransaction());
}

YBCStatus YBCPgSetTransactionIsolationLevel(int isolation) {
  return ToYBCStatus(pgapi->SetTransactionIsolationLevel(isolation));
}

YBCStatus YBCPgSetTransactionReadOnly(bool read_only) {
  return ToYBCStatus(pgapi->SetTransactionReadOnly(read_only));
}

YBCStatus YBCPgEnableFollowerReads(bool enable_follower_reads, int32_t staleness_ms) {
  return ToYBCStatus(pgapi->EnableFollowerReads(enable_follower_reads, staleness_ms));
}

YBCStatus YBCPgSetEnableTracing(bool tracing) {
  return ToYBCStatus(pgapi->SetEnableTracing(tracing));
}

YBCStatus YBCPgSetTransactionDeferrable(bool deferrable) {
  return ToYBCStatus(pgapi->SetTransactionDeferrable(deferrable));
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
      .ysql_enable_create_database_oid_collision_retry =
          &FLAGS_ysql_enable_create_database_oid_collision_retry,
      .ysql_catalog_preload_additional_table_list =
          FLAGS_ysql_catalog_preload_additional_table_list.c_str(),
      .ysql_use_relcache_file                   = &FLAGS_ysql_use_relcache_file,
      .ysql_enable_pg_per_database_oid_allocator =
          &FLAGS_ysql_enable_pg_per_database_oid_allocator,
      .ysql_enable_db_catalog_version_mode =
          &FLAGS_ysql_enable_db_catalog_version_mode,
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
  YBCPgOid database_oid, YBCPgOid table_oid, YBCPgOid index_oid, int row_oid_filtering_attr) {
  pgapi->RegisterSysTableForPrefetching(
      PgObjectId(database_oid, table_oid),
      index_oid == kPgInvalidOid ? PgObjectId() : PgObjectId(database_oid, index_oid),
      row_oid_filtering_attr);
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

uint64_t YBCPgGetReadTimeSerialNo() { return pgapi->GetReadTimeSerialNo(); }

void YBCPgForceReadTimeSerialNo(uint64_t read_time_serial_no) {
  pgapi->ForceReadTimeSerialNo(read_time_serial_no);
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
    uint64_t* current_tserver_ht,
    void callback(void* callback_param, const char* key, size_t key_size), void* callback_param) {
  auto res = pgapi->GetTableKeyRanges(
      PgObjectId(database_oid, table_relfilenode_oid), Slice(lower_bound_key, lower_bound_key_size),
      Slice(upper_bound_key, upper_bound_key_size), max_num_ranges, range_size_bytes, is_forward,
      max_key_length);
  if (!res.ok()) {
    return ToYBCStatus(res.status());
  }

  auto& encoded_table_range_slices = res->encoded_range_end_keys;
  if (!is_forward) {
    return ToYBCStatus(
        STATUS(NotSupported, "YBCGetTableKeyRanges is not supported yet for reverse order"));
  }

  if (current_tserver_ht) {
    *current_tserver_ht = res->current_ht.ToUint64();
  }

  for (size_t i = 0; i < encoded_table_range_slices.size(); ++i) {
    Slice encoded_tablet_ranges = encoded_table_range_slices[i].AsSlice();
    while (!encoded_tablet_ranges.empty()) {
      // Consume null-flag
      const auto s = encoded_tablet_ranges.consume_byte(0);
      if (!s.ok()) {
        return ToYBCStatus(s);
      }
      // Read key from buffer.
      const auto key_size = PgWire::ReadNumber<uint64_t>(&encoded_tablet_ranges);
      Slice key(encoded_tablet_ranges.cdata(), key_size);
      encoded_tablet_ranges.remove_prefix(key_size);

      callback(callback_param, key.cdata(), key.size());
    }
  }

  return YBCStatusOK();
}

//--------------------------------------------------------------------------------------------------
// Replication Slots.

YBCStatus YBCPgNewCreateReplicationSlot(const char *slot_name,
                                        YBCPgOid database_oid,
                                        YBCPgReplicationSlotSnapshotAction snapshot_action,
                                        YBCPgStatement *handle) {
  return ToYBCStatus(pgapi->NewCreateReplicationSlot(slot_name,
                                                     database_oid,
                                                     snapshot_action,
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

YBCStatus YBCPgListReplicationSlots(
    YBCReplicationSlotDescriptor **replication_slots, size_t *numreplicationslots) {
  const auto result = pgapi->ListReplicationSlots();
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }

  const auto &replication_slots_info = result.get().replication_slots();
  *DCHECK_NOTNULL(numreplicationslots) = replication_slots_info.size();
  *DCHECK_NOTNULL(replication_slots) = NULL;
  if (!replication_slots_info.empty()) {
    *replication_slots = static_cast<YBCReplicationSlotDescriptor *>(
        YBCPAlloc(sizeof(YBCReplicationSlotDescriptor) * replication_slots_info.size()));
    YBCReplicationSlotDescriptor *dest = *replication_slots;
    for (const auto &info : replication_slots_info) {
      new (dest) YBCReplicationSlotDescriptor{
          .slot_name = YBCPAllocStdString(info.slot_name()),
          .stream_id = YBCPAllocStdString(info.stream_id()),
          .database_oid = info.database_oid(),
          .active = info.replication_slot_status() == tserver::ReplicationSlotStatus::ACTIVE,
          .confirmed_flush = info.confirmed_flush_lsn(),
          .restart_lsn = info.restart_lsn(),
          .xmin = info.xmin(),
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
  const auto& slot_info = result.get().replication_slot_info();

  *replication_slot =
      static_cast<YBCReplicationSlotDescriptor*>(YBCPAlloc(sizeof(YBCReplicationSlotDescriptor)));

  new (*replication_slot) YBCReplicationSlotDescriptor{
      .slot_name = YBCPAllocStdString(slot_info.slot_name()),
      .stream_id = YBCPAllocStdString(slot_info.stream_id()),
      .database_oid = slot_info.database_oid(),
      .active = slot_info.replication_slot_status() == tserver::ReplicationSlotStatus::ACTIVE,
      .confirmed_flush = slot_info.confirmed_flush_lsn(),
      .restart_lsn = slot_info.restart_lsn(),
      .xmin = slot_info.xmin()
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
    AshCopyTServerSamples(get_cb_slot_fn, result->flush_and_compaction_wait_states(), sample_time);
    AshCopyTServerSamples(get_cb_slot_fn, result->raft_log_appender_wait_states(), sample_time);
    AshCopyTServerSamples(get_cb_slot_fn, result->cql_wait_states(), sample_time);
  }
}

YBCStatus YBCPgInitVirtualWalForCDC(
    const char *stream_id, const YBCPgOid database_oid, YBCPgOid *relations, size_t num_relations) {
  std::vector<PgObjectId> tables;
  tables.reserve(num_relations);

  for (size_t i = 0; i < num_relations; i++) {
    PgObjectId table_id(database_oid, relations[i]);
    tables.push_back(std::move(table_id));
  }

  const auto result = pgapi->InitVirtualWALForCDC(std::string(stream_id), tables);
  if (!result.ok()) {
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
    default:
      LOG(FATAL) << Format("Received unexpected operation $0", row_message_pb.op());
      return YB_PG_ROW_MESSAGE_ACTION_UNKNOWN;
  }
}

YBCStatus YBCPgGetCDCConsistentChanges(
    const char *stream_id, YBCPgChangeRecordBatch **record_batch) {
  const auto result = pgapi->GetConsistentChangesForCDC(std::string(stream_id));
  if (!result.ok()) {
    return ToYBCStatus(result.status());
  }

  *DCHECK_NOTNULL(record_batch) = NULL;
  const auto resp = result.get();
  VLOG(4) << "The GetConsistentChangesForCDC response: " << resp.DebugString();
  auto row_count = resp.cdc_sdk_proto_records_size();

  auto resp_rows_pb = resp.cdc_sdk_proto_records();
  auto resp_rows = static_cast<YBCPgRowMessage *>(YBCPAlloc(sizeof(YBCPgRowMessage) * row_count));
  size_t row_idx = 0;
  for (const auto& row_pb : resp_rows_pb) {
    auto row_message_pb = row_pb.row_message();
    auto commit_time = row_message_pb.commit_time();

    static constexpr size_t OMITTED_VALUE = std::numeric_limits<size_t>::max();

    // column_name -> (index in old tuples, index in new tuples).
    // OMITTED_VALUE indicates omission.
    // For insert: all the columns will be of form (OMITTED_VALUE, <index>).
    // For delete: all the columns will be of form (<index>, OMITTED_VALUE) or
    // won't exist in the map depending on the record type (replica identity) of the stream.
    // For update: both old and new tuples will be present.
    std::unordered_map<std::string, std::pair<size_t, size_t>> col_name_idx_map;
    int new_tuple_idx = 0;
    for (auto &new_tuple : row_message_pb.new_tuple()) {
      if (new_tuple.has_column_name()) {
        col_name_idx_map.emplace(
            new_tuple.column_name(), std::make_pair(OMITTED_VALUE, new_tuple_idx));
      }
      new_tuple_idx++;
    }
    int old_tuple_idx = 0;
    for (auto& old_tuple : row_message_pb.old_tuple()) {
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

    auto col_count = narrow_cast<int>(col_name_idx_map.size());
    YBCPgDatumMessage *cols = nullptr;
    if (col_count > 0) {
      cols = static_cast<YBCPgDatumMessage *>(YBCPAlloc(sizeof(YBCPgDatumMessage) * col_count));

      int tuple_idx = 0;
      for (const auto& col_idxs : col_name_idx_map) {
        YBCPgTypeAttrs type_attrs{-1 /* typmod */};
        const auto& column_name = col_idxs.first;

        // Old value.
        uint64 old_datum = 0;
        bool old_is_null = true;
        if (col_idxs.second.first != OMITTED_VALUE) {
          const auto old_datum_pb =
              &row_message_pb.old_tuple(static_cast<int>(col_idxs.second.first));
          const auto *type_entity =
              pgapi->FindTypeEntity(static_cast<int>(old_datum_pb->column_type()));
          auto s = PBToDatum(
              type_entity, type_attrs, old_datum_pb->pg_ql_value(), &old_datum, &old_is_null);
          if (!s.ok()) {
            return ToYBCStatus(s);
          }
        }

        // New value.
        uint64 new_datum = 0;
        bool new_is_null = true;
        if (col_idxs.second.second != OMITTED_VALUE) {
          const auto new_datum_pb =
              &row_message_pb.new_tuple(static_cast<int>(col_idxs.second.second));
          const auto *type_entity =
              pgapi->FindTypeEntity(static_cast<int>(new_datum_pb->column_type()));
          auto s = PBToDatum(
              type_entity, type_attrs, new_datum_pb->pg_ql_value(), &new_datum, &new_is_null);
          if (!s.ok()) {
            return ToYBCStatus(s);
          }
        }

        auto col = &cols[tuple_idx++];
        col->column_name = YBCPAllocStdString(column_name);
        col->after_op_datum = new_datum;
        col->after_op_is_null = new_is_null;
        col->before_op_datum = old_datum;
        col->before_op_is_null = old_is_null;
      }
    }

    // Only present for DML records.
    YBCPgOid table_oid = kPgInvalidOid;
    if (row_message_pb.has_table_id()) {
      auto table_id = PgObjectId(row_message_pb.table_id());
      table_oid = table_id.object_oid;
    }

    new (&resp_rows[row_idx]) YBCPgRowMessage{
        .col_count = col_count,
        .cols = cols,
        .commit_time = commit_time,
        .action = GetRowMessageAction(row_message_pb),
        .table_oid = table_oid,
        .lsn = row_message_pb.pg_lsn(),
        .xid = row_message_pb.pg_transaction_id()
    };
    row_idx++;
  }

  *record_batch = static_cast<YBCPgChangeRecordBatch *>(YBCPAlloc(sizeof(YBCPgChangeRecordBatch)));
  new (*record_batch) YBCPgChangeRecordBatch{
      .row_count = row_count,
      .rows = resp_rows,
  };

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

} // extern "C"

} // namespace yb::pggate
