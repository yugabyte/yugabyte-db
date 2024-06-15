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

#include "yb/yql/pggate/util/ybc_util.h"

#include <stdarg.h>

#include <fstream>

#include "yb/ash/wait_state.h"

#include "yb/common/pgsql_error.h"
#include "yb/common/transaction_error.h"
#include "yb/common/wire_protocol.h"

#include "yb/gutil/stringprintf.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/debug-util.h"
#include "yb/util/enums.h"
#include "yb/util/env.h"
#include "yb/util/flags.h"
#include "yb/util/init.h"
#include "yb/util/logging.h"
#include "yb/util/net/net_util.h"
#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/thread.h"

#include "yb/yql/pggate/util/ybc-internal.h"

using std::string;
DEFINE_test_flag(string, process_info_dir, string(),
                 "Directory where all postgres process will writes their PIDs and executable name");

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

  // Also allow overriding flags on the command line using the appropriate environment variables.
  std::vector<google::CommandLineFlagInfo> flag_infos;
  google::GetAllFlags(&flag_infos);
  for (auto& flag_info : flag_infos) {
    string env_var_name = "FLAGS_" + flag_info.name;
    const char* env_var_value = getenv(env_var_name.c_str());
    if (env_var_value) {
      google::SetCommandLineOption(flag_info.name.c_str(), env_var_value);
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

// Wraps Status object created by YBCStatus.
// Uses trick with AddRef::kFalse and DetachStruct, to avoid incrementing and decrementing
// ref counter.
class StatusWrapper {
 public:
  explicit StatusWrapper(YBCStatus s) : status_(s, AddRef::kFalse) {}

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

YBPgErrorCode FetchErrorCode(YBCStatus s) {
  StatusWrapper wrapper(s);
  const uint8_t* pg_err_ptr = wrapper->ErrorData(PgsqlErrorTag::kCategory);
  // If we have PgsqlError explicitly set, we decode it
  YBPgErrorCode result = pg_err_ptr != nullptr ? PgsqlErrorTag::Decode(pg_err_ptr)
                                               : YBPgErrorCode::YB_PG_INTERNAL_ERROR;

  // If the error is the default generic YB_PG_INTERNAL_ERROR (as we also set in AsyncRpc::Failed)
  // then we try to deduce it from a transaction error.
  if (result == YBPgErrorCode::YB_PG_INTERNAL_ERROR) {
    const uint8_t* txn_err_ptr = wrapper->ErrorData(TransactionErrorTag::kCategory);
    if (txn_err_ptr != nullptr) {
      switch (TransactionErrorTag::Decode(txn_err_ptr)) {
        case TransactionErrorCode::kAborted: FALLTHROUGH_INTENDED;
        case TransactionErrorCode::kReadRestartRequired: FALLTHROUGH_INTENDED;
        case TransactionErrorCode::kConflict:
          result = YBPgErrorCode::YB_PG_T_R_SERIALIZATION_FAILURE;
          break;
        case TransactionErrorCode::kSnapshotTooOld:
          result = YBPgErrorCode::YB_PG_SNAPSHOT_TOO_OLD;
          break;
        case TransactionErrorCode::kDeadlock:
          result = YBPgErrorCode::YB_PG_T_R_DEADLOCK_DETECTED;
          break;
        case TransactionErrorCode::kNone: FALLTHROUGH_INTENDED;
        default:
          break;
      }
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
    DCHECK(false);
    return nullptr;
  }
  return name + 1;
}

} // anonymous namespace

extern "C" {

bool YBCStatusIsNotFound(YBCStatus s) {
  return StatusWrapper(s)->IsNotFound();
}

// Checks if the status corresponds to an "Unknown Session" error
bool YBCStatusIsUnknownSession(YBCStatus s) {
  // The semantics of the "Unknown session" error is overloaded. It is used to indicate both:
  // 1. Session with an invalid ID
  // 2. An expired session
  // We would like to terminate the connection only in case of an expired session.
  // However, since we are unable to distinguish between the two, we handle both cases identically.
  return StatusWrapper(s)->IsInvalidArgument() &&
         FetchErrorCode(s) == YBPgErrorCode::YB_PG_CONNECTION_DOES_NOT_EXIST;
}

bool YBCStatusIsDuplicateKey(YBCStatus s) {
  return StatusWrapper(s)->IsAlreadyPresent();
}

bool YBCStatusIsSnapshotTooOld(YBCStatus s) {
  return FetchErrorCode(s) == YBPgErrorCode::YB_PG_SNAPSHOT_TOO_OLD;
}

bool YBCStatusIsTryAgain(YBCStatus s) {
  return StatusWrapper(s)->IsTryAgain();
}

bool YBCStatusIsAlreadyPresent(YBCStatus s) {
  return StatusWrapper(s)->IsAlreadyPresent();
}

bool YBCStatusIsReplicationSlotLimitReached(YBCStatus s) {
  return StatusWrapper(s)->IsReplicationSlotLimitReached();
}

bool YBCStatusIsFatalError(YBCStatus s) {
  return YBCStatusIsUnknownSession(s);
}

uint32_t YBCStatusPgsqlError(YBCStatus s) {
  return to_underlying(FetchErrorCode(s));
}

uint16_t YBCStatusTransactionError(YBCStatus s) {
  return to_underlying(TransactionError(*StatusWrapper(s)).value());
}

void YBCFreeStatus(YBCStatus s) {
  FreeYBCStatus(s);
}

const char* YBCStatusFilename(YBCStatus s) {
  return YBCPAllocStdString(StatusWrapper(s)->file_name());
}

int YBCStatusLineNumber(YBCStatus s) {
  return StatusWrapper(s)->line_number();
}

const char* YBCStatusFuncname(YBCStatus s) {
  const std::string funcname_str =
    FuncNameTag::Decode(StatusWrapper(s)->ErrorData(FuncNameTag::kCategory));
  return funcname_str.empty() ? nullptr : YBCPAllocStdString(funcname_str);
}

size_t YBCStatusMessageLen(YBCStatus s) {
  return StatusWrapper(s)->message().size();
}

const char* YBCStatusMessageBegin(YBCStatus s) {
  return StatusWrapper(s)->message().cdata();
}

const char* YBCMessageAsCString(YBCStatus s) {
  size_t msg_size = YBCStatusMessageLen(s);
  char* msg_buf = static_cast<char*>(YBCPAlloc(msg_size + 1));
  memcpy(msg_buf, YBCStatusMessageBegin(s), msg_size);
  msg_buf[msg_size] = 0;
  return msg_buf;
}

unsigned int YBCStatusRelationOid(YBCStatus s) {
  return RelationOidTag::Decode(StatusWrapper(s)->ErrorData(RelationOidTag::kCategory));
}

const char** YBCStatusArguments(YBCStatus s, size_t* nargs) {
  const char** result = nullptr;
  const std::vector<std::string>& args = PgsqlMessageArgsTag::Decode(
      StatusWrapper(s)->ErrorData(PgsqlMessageArgsTag::kCategory));
  if (nargs) {
    *nargs = args.size();
  }
  if (!args.empty()) {
    size_t i = 0;
    result = static_cast<const char**>(YBCPAlloc(args.size() * sizeof(const char*)));
    for (const std::string& arg : args) {
      result[i++] = YBCPAllocStdString(arg);
    }
  }
  return result;
}

bool YBCIsRestartReadError(uint16_t txn_errcode) {
  return txn_errcode == to_underlying(TransactionErrorCode::kReadRestartRequired);
}

bool YBCIsTxnConflictError(uint16_t txn_errcode) {
  return txn_errcode == to_underlying(TransactionErrorCode::kConflict);
}

bool YBCIsTxnSkipLockingError(uint16_t txn_errcode) {
  return txn_errcode == to_underlying(TransactionErrorCode::kSkipLocking);
}

bool YBCIsTxnDeadlockError(uint16_t txn_errcode) {
  return txn_errcode == to_underlying(TransactionErrorCode::kDeadlock);
}

bool YBCIsTxnAbortedError(uint16_t txn_errcode) {
  return txn_errcode == to_underlying(TransactionErrorCode::kAborted);
}

const char* YBCTxnErrCodeToString(uint16_t txn_errcode) {
  return YBCPAllocStdString(ToString(TransactionErrorCode(txn_errcode)));
}

uint16_t YBCGetTxnConflictErrorCode() {
  return to_underlying(TransactionErrorCode::kConflict);
}

YBCStatus YBCInit(const char* argv0,
                  YBCPAllocFn palloc_fn,
                  YBCCStringToTextWithLenFn cstring_to_text_with_len_fn) {
  YBCSetPAllocFn(palloc_fn);
  if (cstring_to_text_with_len_fn) {
    YBCSetCStringToTextWithLenFn(cstring_to_text_with_len_fn);
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
  constexpr uint32_t kWaitEventMask = (1 << YB_ASH_COMPONENT_POSITION) - 1;
  uint32_t wait_event = wait_event_info & kWaitEventMask;
  return NoPrefixName(GetWaitStateType(static_cast<ash::WaitStateCode>(wait_event)));
}

uint8_t YBCGetQueryIdForCatalogRequests() {
  return static_cast<uint8_t>(ash::FixedQueryId::kQueryIdForCatalogRequests);
}

} // extern "C"

} // namespace yb::pggate
