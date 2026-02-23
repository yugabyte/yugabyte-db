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

#include "yb/yql/pggate/util/ybc_util.h"

#include <stdarg.h>

#include <fstream>

#include "catalog/pg_type_d.h"

#include "yb/ash/wait_state.h"

#include "yb/common/entity_ids.h"
#include "yb/common/init.h"
#include "yb/common/pgsql_error.h"
#include "yb/common/transaction_error.h"
#include "yb/common/wire_protocol.h"

#include "yb/dockv/partition.h"

#include "yb/gutil/stringprintf.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/debug-util.h"
#include "yb/util/enums.h"
#include "yb/util/env.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/net/net_util.h"
#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/stack_trace.h"
#include "yb/util/thread.h"

#include "yb/yql/pggate/util/ybc-internal.h"

using std::string;
DEFINE_test_flag(string, process_info_dir, string(),
                 "Directory where all postgres process will writes their PIDs and executable name");
DECLARE_bool(enable_object_locking_for_table_locks);
DECLARE_bool(ysql_enable_auto_analyze_infra);
DECLARE_bool(ysql_enable_auto_analyze);

namespace yb::pggate {

namespace {

void ChangeWorkingDir(const char* dir) {
  int chdir_result = chdir(dir);
  if (chdir_result != 0) {
    LOG(WARNING) << "Failed to change working directory to " << dir << ", error was "
                 << errno << " " << std::strerror(errno) << "!";
  }
}

void WriteCurrentProcessInfo(const string& destination_dir) {
  string executable_path;
  if (Env::Default()->GetExecutablePath(&executable_path).ok()) {
    const auto destination_file = Format("$0/$1", destination_dir, getpid());
    std::ofstream out(destination_file, std::ios_base::out);
    out << executable_path;
    if (out) {
      LOG(INFO) << "Process info is written to " << destination_file;
      return;
    }
  }
  LOG(WARNING) << "Unable to write process info to "
               << destination_dir << " dir: error " << errno << " " << std::strerror(errno);
}

Status InitGFlags(const char* argv0) {

  const char* executable_path = argv0;
  std::string executable_path_str;
  if (executable_path == nullptr) {
    RETURN_NOT_OK(Env::Default()->GetExecutablePath(&executable_path_str));
    executable_path = executable_path_str.c_str();
  }
  RSTATUS_DCHECK(executable_path != nullptr, RuntimeError, "Unable to get path to executable");

  // Change current working directory from postgres data dir (as set by postmaster)
  // to the one from yb-tserver so that relative paths in gflags would be resolved in the same way.
  char pg_working_dir[PATH_MAX];
  CHECK(getcwd(pg_working_dir, sizeof(pg_working_dir)) != nullptr);
  const char* yb_working_dir = getenv("YB_WORKING_DIR");
  if (yb_working_dir) {
    ChangeWorkingDir(yb_working_dir);
  }
  auto se = ScopeExit([&pg_working_dir] {
    // Restore PG data dir as current directory.
    ChangeWorkingDir(pg_working_dir);
  });

  // This is needed in case VLOG/VLOG_IS_ON was called before that and initial vmodule flag
  // value has already been cached.
  RegisterGlobalFlagsCallbacksOnce();

  // Also allow overriding flags on the command line using the appropriate environment variables.
  std::vector<google::CommandLineFlagInfo> flag_infos;
  google::GetAllFlags(&flag_infos);
  for (auto& flag_info : flag_infos) {
    string env_var_name = "FLAGS_" + flag_info.name;
    const char* env_var_value = getenv(env_var_name.c_str());
    if (env_var_value) {
      // Make sure callbacks are called.
      RETURN_NOT_OK(flags_internal::SetFlagInternal(
          flag_info.flag_ptr, flag_info.name.c_str(), env_var_value));
    }
  }

  RETURN_NOT_OK(CheckCPUFlags());
  // Use InitGoogleLoggingSafeBasic() instead of InitGoogleLoggingSafe() to avoid calling
  // google::InstallFailureSignalHandler(). This will prevent interference with PostgreSQL's
  // own signal handling.
  // Use InitGoogleLoggingSafeBasicSuppressNonNativePostgresLogs() over
  // InitGoogleLoggingSafeBasic() when the "suppress_nonpg_logs" flag is set.
  // This will suppress non-Postgres logs from the Postgres log file.
  if (suppress_nonpg_logs) {
    yb::InitGoogleLoggingSafeBasicSuppressNonNativePostgresLogs(executable_path);
  } else {
    yb::InitGoogleLoggingSafeBasic(executable_path);
  }

  if (VLOG_IS_ON(1)) {
    for (auto& flag_info : flag_infos) {
      string env_var_name = "FLAGS_" + flag_info.name;
      const char* env_var_value = getenv(env_var_name.c_str());
      if (env_var_value) {
        VLOG(1) << "Setting flag " << flag_info.name << " to the value of the env var "
                << env_var_name << ": " << env_var_value;
      }
    }
  }

  return Status::OK();
}

// Wraps Status object created by YbcStatus.
// Uses trick with AddRef::kFalse and DetachStruct, to avoid incrementing and decrementing
// ref counter.
class StatusWrapper {
 public:
  explicit StatusWrapper(YbcStatus s) : status_(s, AddRef::kFalse) {}

  ~StatusWrapper() {
    status_.DetachStruct();
  }

  Status* operator->() {
    return &status_;
  }

  Status& operator*() {
    return status_;
  }

 private:
  Status status_;
};

YBPgErrorCode FetchErrorCode(YbcStatus s) {
  StatusWrapper wrapper(s);
  auto result = PgsqlError::ValueFromStatus(*wrapper).value_or(YBPgErrorCode::YB_PG_INTERNAL_ERROR);

  // If there's a schema version mismatch, we need to return the status code 40001.
  // When we get a schema version mismatch, DocDB will set the PgsqlResponsePB::RequestStatus
  // to PGSQL_STATUS_SCHEMA_VERSION_MISMATCH. Note that this is a separate field from the above
  // PgsqlErrorTag.
  if (const auto pgsql_err = PgsqlRequestStatus::ValueFromStatus(*wrapper);
      pgsql_err && *pgsql_err == PgsqlResponsePB::PGSQL_STATUS_SCHEMA_VERSION_MISMATCH) {
    LOG_IF(DFATAL, result != YBPgErrorCode::YB_PG_INTERNAL_ERROR)
        << "Expected schema version mismatch error to be YB_PG_INTERNAL_ERROR, got "
        << ToString(result);
    result = YBPgErrorCode::YB_PG_T_R_SERIALIZATION_FAILURE;
  }

  // If the error is the default generic YB_PG_INTERNAL_ERROR (as we also set in AsyncRpc::Failed)
  // then we try to deduce it from a transaction error.
  if (result == YBPgErrorCode::YB_PG_INTERNAL_ERROR) {
    switch (TransactionError::ValueFromStatus(*wrapper).value_or(TransactionErrorCode::kNone)) {
      case TransactionErrorCode::kNone:
        break;
      case TransactionErrorCode::kAborted:
        result = YBPgErrorCode::YB_PG_YB_TXN_ABORTED;
        break;
      case TransactionErrorCode::kReadRestartRequired:
        result = YBPgErrorCode::YB_PG_YB_RESTART_READ;
        break;
      case TransactionErrorCode::kConflict:
        result = YBPgErrorCode::YB_PG_YB_TXN_CONFLICT;
        break;
      case TransactionErrorCode::kSnapshotTooOld:
        result = YBPgErrorCode::YB_PG_SNAPSHOT_TOO_OLD;
        break;
      case TransactionErrorCode::kDeadlock:
        result = YBPgErrorCode::YB_PG_YB_DEADLOCK;
        break;
      case TransactionErrorCode::kSkipLocking:
        result = YBPgErrorCode::YB_PG_YB_TXN_SKIP_LOCKING;
        break;
      case TransactionErrorCode::kLockNotFound:
        result = YBPgErrorCode::YB_PG_YB_TXN_LOCK_NOT_FOUND;
        break;
      default:
        result = YBPgErrorCode::YB_PG_YB_TXN_ERROR;
        break;
    }
  }
  return result;
}

// Used for ASH to extract the name for a given enum, after
// removing the leading "k"-prefix.
template <class Enum>
const char* NoPrefixName(Enum value) {
  const char* name = ToCString(value);
  if (!name) {
    DCHECK(false) << "Prefix not found for: " << ToString(value);
    return "";
  }
  return name + 1;
}

} // anonymous namespace

extern "C" {

bool YBCStatusIsNotFound(YbcStatus s) {
  return StatusWrapper(s)->IsNotFound();
}

// Checks if the status corresponds to an "Unknown Session" error
bool YBCStatusIsUnknownSession(YbcStatus s) {
  // The semantics of the "Unknown session" error is overloaded. It is used to indicate both:
  // 1. Session with an invalid ID
  // 2. An expired session
  // We would like to terminate the connection only in case of an expired session.
  // However, since we are unable to distinguish between the two, we handle both cases identically.
  return StatusWrapper(s)->IsInvalidArgument() &&
         FetchErrorCode(s) == YBPgErrorCode::YB_PG_CONNECTION_DOES_NOT_EXIST;
}

bool YBCStatusIsSnapshotTooOld(YbcStatus s) {
  return FetchErrorCode(s) == YBPgErrorCode::YB_PG_SNAPSHOT_TOO_OLD;
}

bool YBCStatusIsTryAgain(YbcStatus s) {
  return StatusWrapper(s)->IsTryAgain();
}

bool YBCStatusIsAlreadyPresent(YbcStatus s) {
  return StatusWrapper(s)->IsAlreadyPresent();
}

bool YBCStatusIsReplicationSlotLimitReached(YbcStatus s) {
  return StatusWrapper(s)->IsReplicationSlotLimitReached();
}

bool YBCStatusIsFatalError(YbcStatus s) {
  return YBCStatusIsUnknownSession(s);
}

uint32_t YBCStatusPgsqlError(YbcStatus s) {
  return std::to_underlying(FetchErrorCode(s));
}

void YBCFreeStatus(YbcStatus s) {
  FreeYBCStatus(s);
}

const char* YBCStatusFilename(YbcStatus s) {
  return YBCPAllocStdString(StatusWrapper(s)->file_name());
}

int YBCStatusLineNumber(YbcStatus s) {
  return StatusWrapper(s)->line_number();
}

const char* YBCStatusFuncname(YbcStatus s) {
  const auto funcname = FuncName::ValueFromStatus(*StatusWrapper(s));
  return funcname ? YBCPAllocStdString(*funcname) : nullptr;
}

size_t YBCStatusMessageLen(YbcStatus s) {
  return StatusWrapper(s)->message().size();
}

const char* YBCStatusMessageBegin(YbcStatus s) {
  return StatusWrapper(s)->message().cdata();
}

const char* YBCMessageAsCString(YbcStatus s) {
  size_t msg_size = YBCStatusMessageLen(s);
  char* msg_buf = static_cast<char*>(YBCPAlloc(msg_size + 1));
  memcpy(msg_buf, YBCStatusMessageBegin(s), msg_size);
  msg_buf[msg_size] = 0;
  return msg_buf;
}

unsigned int YBCStatusRelationOid(YbcStatus s) {
  return RelationOid(*StatusWrapper(s)).value();
}

const char** YBCStatusArguments(YbcStatus s, size_t* nargs) {
  const char** result = nullptr;
  const auto args = PgsqlMessageArgs::ValueFromStatus(*StatusWrapper(s));
  if (nargs) {
    *nargs = args ? args->size() : 0;
  }
  if (args && !args->empty()) {
    size_t i = 0;
    result = static_cast<const char**>(YBCPAlloc(args->size() * sizeof(const char*)));
    for (const auto& arg : *args) {
      result[i++] = YBCPAllocStdString(arg);
    }
  }
  return result;
}

YbcStatus YBCInit(const char* argv0,
                  YbcPallocFn palloc_fn,
                  YbcCstringToTextWithLenFn cstring_to_text_with_len_fn,
                  YbcSwitchMemoryContextFn switch_mem_context_fn,
                  YbcCreateMemoryContextFn create_mem_context_fn,
                  YbcDeleteMemoryContextFn delete_mem_context_fn) {
  YBCSetPAllocFn(palloc_fn);
  if (cstring_to_text_with_len_fn) {
    YBCSetCStringToTextWithLenFn(cstring_to_text_with_len_fn);
  }
  if (switch_mem_context_fn) {
    YBCSetSwitchMemoryContextFn(switch_mem_context_fn);
  }
  if (create_mem_context_fn) {
    YBCSetCreateMemoryContextFn(create_mem_context_fn);
  }
  if (delete_mem_context_fn) {
    YBCSetDeleteMemoryContextFn(delete_mem_context_fn);
  }
  auto status = InitGFlags(argv0);
  if (status.ok() && !FLAGS_TEST_process_info_dir.empty()) {
    WriteCurrentProcessInfo(FLAGS_TEST_process_info_dir);
  }
  return ToYBCStatus(status);
}

void YBCLogImpl(
    google::LogSeverity severity,
    const char* file,
    int line,
    bool with_stack_trace,
    const char* format,
    ...) {
  va_list argptr;
  va_start(argptr, format);
  YBCLogVA(severity, file, line, with_stack_trace, format, argptr);
  va_end(argptr);
}

void YBCLogVA(
    google::LogSeverity severity,
    const char* file,
    int line,
    bool with_stack_trace,
    const char* format,
    va_list argptr) {
  string buf;
  StringAppendV(&buf, format, argptr);
  google::LogMessage log_msg(file, line, severity);
  log_msg.stream() << buf;
  if (with_stack_trace) {
    log_msg.stream() << "\n" << yb::GetStackTrace();
  }
}

const char* YBCFormatBytesAsStr(const char* data, size_t size) {
  return YBCPAllocStdString(FormatBytesAsStr(data, size));
}

const char* YBCGetStackTrace() {
  return YBCPAllocStdString(yb::GetStackTrace());
}

void YBCResolveHostname() {
  string fqdn;
  auto status = GetFQDN(&fqdn);
  if (!status.ok()) {
    LOG(WARNING) << "Failed to get fully qualified domain name of the local hostname: "
                 << status;
  }
}

inline double YBCGetNumHashBuckets() {
  return 64.0;
}

/* Gets the number of hash buckets for a DocDB table */
inline double YBCGetHashBucketFromValue(uint32_t hash_val) {
  /*
  * Since hash values are 16 bit for now and there are (1 << 6)
  * buckets, we must right shift a hash value by 16 - 6 = 10 to
  * obtain its bucket number
  */
  return hash_val >> 10;
}

double YBCEvalHashValueSelectivity(int32_t hash_low, int32_t hash_high) {
      hash_high = hash_high <= USHRT_MAX ? hash_high : USHRT_MAX;
      hash_high = hash_high >= 0 ? hash_high : 0;
      hash_low = hash_low >= 0 ? hash_low : 0;
      hash_low = hash_low <= USHRT_MAX ? hash_low : USHRT_MAX;

      uint32_t greatest_bucket = YBCGetHashBucketFromValue(hash_high);
      uint32_t lowest_bucket = YBCGetHashBucketFromValue(hash_low);
      return hash_high >= hash_low ?
          ((greatest_bucket - lowest_bucket + 1.0) / YBCGetNumHashBuckets())
          : 0.0;
}

void YBCInitThreading() {
  InitThreading();
}

void YBCGenerateAshRootRequestId(unsigned char *root_request_id) {
  uint64_t a = RandomUniformInt<uint64_t>();
  uint64_t b = RandomUniformInt<uint64_t>();
  std::memcpy(root_request_id, &a, sizeof(uint64_t));
  std::memcpy(root_request_id + sizeof(uint64_t), &b, sizeof(uint64_t));
}

// This is our version of pgstat_get_wait_event
const char* YBCGetWaitEventName(uint32_t wait_event_info) {
  constexpr uint32_t kWaitEventMask = (1 << YB_ASH_COMPONENT_POSITION) - 1;
  uint32_t wait_event = wait_event_info & kWaitEventMask;
  return NoPrefixName(static_cast<ash::WaitStateCode>(wait_event));
}

// 32 bit representation of the wait event.
// <4-bit Component> <4-bit Class> <8-bit Reserved> <16-bit Event>
// The classes of the wait events defined by us can give more idea about where in the code
// the wait event can show up, or what those events are supposed to be doing.
// For example, it can be RocksDB, Consensus, TabletWait.
//
// But for the wait events defined by PG, wait event type is encoded in place of the 'class' bits,
// so we have a generic class (YSQLQuery) for all these wait events.
//
// Check wait_state.h for more information about how the wait events are encoded.
const char* YBCGetWaitEventClass(uint32_t wait_event_info) {
  // The next 4 bits after the highest 4 bits are needed to get the wait event class
  constexpr uint8_t kAshClassMask = (1 << YB_ASH_CLASS_BITS) - 1;
  uint8_t class_id = narrow_cast<uint8_t>(wait_event_info >> YB_ASH_CLASS_POSITION) & kAshClassMask;
  uint8_t comp_id = narrow_cast<uint8_t>(wait_event_info >> YB_ASH_COMPONENT_POSITION);

  // Zero wait_event_info is used by PG while it's processing the query and not waiting on anything.
  // If it's non-zero, then we check if the component is YSQL and that the class is not something
  // we defined in ash::Class
  const bool is_wait_event_defined_by_pg = wait_event_info == 0 ||
      (static_cast<ash::Component>(comp_id) == ash::Component::kYSQL &&
       static_cast<ash::Class>(class_id) != ash::Class::kTServerWait);

  if (is_wait_event_defined_by_pg)
    return "YSQLQuery";

  return NoPrefixName(static_cast<ash::Class>(class_id));
}

const char* YBCGetWaitEventComponent(uint32_t wait_event_info) {
  // The highest 4 bits are needed to get the wait event component
  uint8_t comp_id = narrow_cast<uint8_t>(wait_event_info >> YB_ASH_COMPONENT_POSITION);
  return NoPrefixName(static_cast<ash::Component>(comp_id));
}

// This is our version of pgstat_get_wait_event_type
const char* YBCGetWaitEventType(uint32_t wait_event_info) {
  // This is only called for ASH wait events, so we remove the component bits as
  // ash::WaitStateCode doesn't contain them.
  static constexpr uint32_t kWaitEventMask = (1 << YB_ASH_COMPONENT_POSITION) - 1;
  uint32_t wait_event = wait_event_info & kWaitEventMask;
  return NoPrefixName(GetWaitStateType(static_cast<ash::WaitStateCode>(wait_event)));
}

const char* YBCGetWaitEventAuxDescription(uint32_t wait_event_info) {
  static constexpr uint32_t kWaitEventMask = (1 << YB_ASH_COMPONENT_POSITION) - 1;
  uint32_t wait_event = wait_event_info & kWaitEventMask;
  const char* result = ash::GetWaitStateAuxDescription(static_cast<ash::WaitStateCode>(wait_event));
  return result;
}

uint8_t YBCGetConstQueryId(YbcAshConstQueryIdType type) {
  switch (type) {
    case YbcAshConstQueryIdType::QUERY_ID_TYPE_DEFAULT:
      return static_cast<uint8_t>(ash::FixedQueryId::kQueryIdForUncomputedQueryId);
    case YbcAshConstQueryIdType::QUERY_ID_TYPE_BACKGROUND_WORKER:
      return static_cast<uint8_t>(ash::FixedQueryId::kQueryIdForYSQLBackgroundWorker);
    case YbcAshConstQueryIdType::QUERY_ID_TYPE_WALSENDER:
      return static_cast<uint8_t>(ash::FixedQueryId::kQueryIdForWalsender);
  }
  FATAL_INVALID_ENUM_VALUE(YbcAshConstQueryIdType, type);
}

uint32_t YBCWaitEventForWaitingOnTServer() {
  return std::to_underlying(ash::WaitStateCode::kWaitingOnTServer);
}

// Get a random integer between a and b
int YBCGetRandomUniformInt(int a, int b) {
  return RandomUniformInt<int>(a, b);
}

YbcWaitEventDescriptor YBCGetWaitEventDescription(size_t index) {
  static const auto desc = ash::WaitStateInfo::GetWaitStatesDescription();
  if (index < desc.size()) {
    // Fill up the component bits with non-zero value so that when YBCGetWaitEventClass
    // is called later from yb_wait_event_desc, it doesn't report YSQLQuery as the class
    // for most cases. It's fine because we don't use the component bits here for
    // anything else.
    uint32_t code = ash::WaitStateInfo::AshNormalizeComponentForTServerEvents(
      static_cast<uint32_t>(desc[index].code), false);
    return { code, desc[index].description.c_str() };
  }
  return { 0, nullptr };
}

int YBCGetCircularBufferSizeInKiBs() {
  return ash::WaitStateInfo::GetCircularBufferSizeInKiBs();
}

const char* YBCGetPggateRPCName(uint32_t pggate_rpc_enum_value) {
  return NoPrefixName(static_cast<ash::PggateRPC>(pggate_rpc_enum_value));
}

uint32_t YBCAshNormalizeComponentForTServerEvents(uint32_t code, bool component_bits_set) {
  return ash::WaitStateInfo::AshNormalizeComponentForTServerEvents(code, component_bits_set);
}

int YBCGetCallStackFrames(void** result, int max_depth, int skip_count) {
  return google::GetStackTrace(result, max_depth, skip_count);
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

bool YBIsMajorUpgradeInitDb() {
  static int cached_value = -1;
  if (cached_value == -1) {
    const char* env_var_value = getenv("YB_PG_MAJOR_UPGRADE_INITDB");
    cached_value = env_var_value && strcmp(env_var_value, "true") == 0;
  }

  return cached_value;
}


const char *YBCGetOutFuncName(YbcPgOid typid) {
  switch (typid) {
    case BOOLOID:
      return "boolout";
    case BYTEAOID:
      return "byteaout";
    case CHAROID:
      return "charout";
    case NAMEOID:
      return "nameout";
    case INT8OID:
      return "int8out";
    case INT2OID:
      return "int2out";
    case INT4OID:
      return "int4out";
    case REGPROCOID:
      return "regprocout";
    case TEXTOID:
      return "textout";
    case TIDOID:
      return "tidout";
    case XIDOID:
      return "xidout";
    case CIDOID:
      return "cidout";
    case JSONOID:
      return "json_out";
    case XMLOID:
      return "xml_out";
    case POINTOID:
      return "point_out";
    case LSEGOID:
      return "lseg_out";
    case PATHOID:
      return "path_out";
    case BOXOID:
      return "box_out";
    case LINEOID:
      return "line_out";
    case FLOAT4OID:
      return "float4out";
    case FLOAT8OID:
      return "float8out";
    case CIRCLEOID:
      return "circle_out";
    case CASHOID:
      return "cash_out";
    case MACADDROID:
      return "macaddr_out";
    case INETOID:
      return "inet_out";
    case CIDROID:
      return "cidr_out";
    case MACADDR8OID:
      return "macaddr8_out";
    case ACLITEMOID:
      return "aclitemout";
    case BPCHAROID:
      return "bpcharout";
    case VARCHAROID:
      return "varcharout";
    case DATEOID:
      return "date_out";
    case TIMEOID:
      return "time_out";
    case TIMESTAMPOID:
      return "timestamp_out";
    case TIMESTAMPTZOID:
      return "timestamptz_out";
    case INTERVALOID:
      return "interval_out";
    case TIMETZOID:
      return "timetz_out";
    case BITOID:
      return "bit_out";
    case VARBITOID:
      return "varbit_out";
    case NUMERICOID:
      return "numeric_out";
    case REGPROCEDUREOID:
      return "regprocedureout";
    case REGOPEROID:
      return "regoperout";
    case REGOPERATOROID:
      return "regoperatorout";
    case REGCLASSOID:
      return "regclassout";
    case REGTYPEOID:
      return "regtypeout";
    case REGROLEOID:
      return "regroleout";
    case REGNAMESPACEOID:
      return "regnamespaceout";
    case UUIDOID:
      return "uuid_out";
    case LSNOID:
      return "pg_lsn_out";
    case TSQUERYOID:
      return "tsqueryout";
    case REGCONFIGOID:
      return "regconfigout";
    case REGDICTIONARYOID:
      return "regdictionaryout";
    case JSONBOID:
      return "jsonb_out";
    case TXID_SNAPSHOTOID:
      return "pg_snapshot_out";
    case RECORDOID:
      return "record_out";
    case CSTRINGOID:
      return "cstring_out";
    case ANYOID:
      return "any_out";
    case VOIDOID:
      return "void_out";
    case TRIGGEROID:
      return "trigger_out";
    case LANGUAGE_HANDLEROID:
      return "language_handler_out";
    case INTERNALOID:
      return "internal_out";
    case ANYELEMENTOID:
      return "anyelement_out";
    case ANYNONARRAYOID:
      return "anynonarray_out";
    case ANYENUMOID:
      return "anyenum_out";
    case FDW_HANDLEROID:
      return "fdw_handler_out";
    case INDEX_AM_HANDLEROID:
      return "index_am_handler_out";
    case TSM_HANDLEROID:
      return "tsm_handler_out";
    case ANYRANGEOID:
      return "anyrange_out";
    case INT2VECTOROID:
      return "int2vectorout";
    case OIDVECTOROID:
      return "oidvectorout";
    case TSVECTOROID:
      return "tsvectorout";
    case GTSVECTOROID:
      return "gtsvectorout";
    case POLYGONOID:
      return "poly_out";
    case INT4RANGEOID:
      return "int4out";
    case NUMRANGEOID:
      return "numeric_out";
    case TSRANGEOID:
      return "timestamp_out";
    case TSTZRANGEOID:
      return "timestamptz_out";
    case DATERANGEOID:
      return "date_out";
    case INT8RANGEOID:
      return "int8out";
    case XMLARRAYOID:
      return "xml_out";
    case LINEARRAYOID:
      return "line_out";
    case CIRCLEARRAYOID:
      return "circle_out";
    case MONEYARRAYOID:
      return "cash_out";
    case BOOLARRAYOID:
      return "boolout";
    case BYTEAARRAYOID:
      return "byteaout";
    case CHARARRAYOID:
      return "charout";
    case NAMEARRAYOID:
      return "nameout";
    case INT2ARRAYOID:
      return "int2out";
    case INT2VECTORARRAYOID:
      return "int2vectorout";
    case INT4ARRAYOID:
      return "int4out";
    case REGPROCARRAYOID:
      return "regprocout";
    case TEXTARRAYOID:
      return "textout";
    case OIDARRAYOID:
      return "oidout";
    case CIDRARRAYOID:
      return "cidr_out";
    case TIDARRAYOID:
      return "tidout";
    case XIDARRAYOID:
      return "xidout";
    case CIDARRAYOID:
      return "cidout";
    case OIDVECTORARRAYOID:
      return "oidvectorout";
    case BPCHARARRAYOID:
      return "bpcharout";
    case VARCHARARRAYOID:
      return "varcharout";
    case INT8ARRAYOID:
      return "int8out";
    case POINTARRAYOID:
      return "point_out";
    case LSEGARRAYOID:
      return "lseg_out";
    case PATHARRAYOID:
      return "path_out";
    case BOXARRAYOID:
      return "box_out";
    case FLOAT4ARRAYOID:
      return "float4out";
    case FLOAT8ARRAYOID:
      return "float8out";
    case ACLITEMARRAYOID:
      return "aclitemout";
    case MACADDRARRAYOID:
      return "macaddr_out";
    case MACADDR8ARRAYOID:
      return "macaddr8_out";
    case INETARRAYOID:
      return "inet_out";
    case CSTRINGARRAYOID:
      return "cstring_out";
    case TIMESTAMPARRAYOID:
      return "timestamp_out";
    case DATEARRAYOID:
      return "date_out";
    case TIMEARRAYOID:
      return "time_out";
    case TIMESTAMPTZARRAYOID:
      return "timestamptz_out";
    case INTERVALARRAYOID:
      return "interval_out";
    case NUMERICARRAYOID:
      return "numeric_out";
    case TIMETZARRAYOID:
      return "timetz_out";
    case BITARRAYOID:
      return "bit_out";
    case VARBITARRAYOID:
      return "varbit_out";
    case REGPROCEDUREARRAYOID:
      return "regprocedureout";
    case REGOPERARRAYOID:
      return "regoperout";
    case REGOPERATORARRAYOID:
      return "regoperatorout";
    case REGCLASSARRAYOID:
      return "regclassout";
    case REGTYPEARRAYOID:
      return "regtypeout";
    case REGROLEARRAYOID:
      return "regroleout";
    case REGNAMESPACEARRAYOID:
      return "regnamespaceout";
    case UUIDARRAYOID:
      return "uuid_out";
    case PG_LSNARRAYOID:
      return "pg_lsn_out";
    case TSVECTORARRAYOID:
      return "tsvectorout";
    case GTSVECTORARRAYOID:
      return "gtsvectorout";
    case TSQUERYARRAYOID:
      return "tsqueryout";
    case REGCONFIGARRAYOID:
      return "regconfigout";
    case REGDICTIONARYARRAYOID:
      return "regdictionaryout";
    case JSONARRAYOID:
      return "json_out";
    case JSONBARRAYOID:
      return "jsonb_out";
    case TXID_SNAPSHOTARRAYOID:
      return "pg_snapshot_out";
    case RECORDARRAYOID:
      return "record_out";
    case ANYARRAYOID:
      return "any_out";
    case POLYGONARRAYOID:
      return "poly_out";
    case INT4RANGEARRAYOID:
      return "int4out";
    case NUMRANGEARRAYOID:
      return "numeric_out";
    case TSRANGEARRAYOID:
      return "timestamp_out";
    case TSTZRANGEARRAYOID:
      return "timestamptz_out";
    case DATERANGEARRAYOID:
      return "date_out";
    case INT8RANGEARRAYOID:
      return "int8out";
    default:
      return NULL;
  }
}

namespace {
YbcUpdateInitPostgresMetricsFn update_init_postgres_metrics_fn = nullptr;
}  // namespace

void
YBCSetUpdateInitPostgresMetricsFn(YbcUpdateInitPostgresMetricsFn update_init_postgres_metrics) {
  CHECK_NOTNULL(update_init_postgres_metrics);
  update_init_postgres_metrics_fn = update_init_postgres_metrics;
}

void YBCUpdateInitPostgresMetrics() {
  if (update_init_postgres_metrics_fn) {
    update_init_postgres_metrics_fn();
  } else {
    // At initdb time we do not load yb_pg_metrics extension.
    DCHECK(YBCIsInitDbModeEnvVarSet());
  }
}

uint16_t YBCDecodeMultiColumnHashLeftBound(const char* partition_key, size_t key_len) {
  yb::Slice slice(partition_key, key_len);
  return dockv::PartitionSchema::DecodePartitionKeyStartAsHashLeftBoundInclusive(slice);
}

uint16_t YBCDecodeMultiColumnHashRightBound(const char* partition_key, size_t key_len) {
  yb::Slice slice(partition_key, key_len);
  return CHECK_RESULT(
      dockv::PartitionSchema::DecodePartitionKeyEndAsHashRightBoundInclusive(slice));
}

bool
YBCIsLegacyModeForCatalogOps() {
  //
  // If object locking is enabled:
  //
  // (1) Catalog writes will use the CatalogSnapshot's read time serial number instead of the
  //     TransactionSnapshot's read time serial number (which is the legacy pre-object locking
  //     behavior). This is required to allow concurrent DDLs by not causing write-write conflicts
  //     based on overlapping [transaction read time, commit time] windows. The serialization of
  //     catalog modifications via DDLs is now handled by object locks. Catalog writes were using
  //     the kTransactional session type pre-object locking and that stays the same.
  //
  // (2) Catalog reads will always use the kTransactional session type. This is done so that they
  //     can also use the CatalogSnapshot's read time serial number to read the latest data (and)
  //     see the catalog data modified by the current active transaction (this is required because
  //     transactional DDL is enabled if object locking is enabled).
  //
  //     In the pre-object locking mode, catalog reads for DML transactions go via the kCatalog
  //     session type which has a single catalog_read_time_ (see pg_session.h). Catalog reads
  //     executed as part of a DDL transaction (or) after a DDL in a DDL-DML transaction block
  //     (i.e., with transactional DDL enabled) go via the kTransactional session type and would use
  //     the TransactionSnapshot's read time serial number.
  //
  return !YBCIsObjectLockingEnabled() || !yb_enable_concurrent_ddl || YBCIsInitDbModeEnvVarSet();
}

bool YBCIsObjectLockingEnabled() {
  return FLAGS_enable_object_locking_for_table_locks && enable_object_locking_infra;
}

bool YBCIsAutoAnalyzeEnabled() {
  return FLAGS_ysql_enable_auto_analyze_infra && FLAGS_ysql_enable_auto_analyze;
}

} // extern "C"

} // namespace yb::pggate
