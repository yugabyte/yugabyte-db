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
//
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/test/pggate_test.h"

#include <memory>
#include <string>
#include <unordered_set>

#include "yb/util/flags.h"

#include "yb/common/entity_ids.h"
#include "yb/common/pg_types.h"

#include "yb/gutil/ref_counted.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tserver/tserver_util_fwd.h"
#include "yb/tserver/tserver_service.proxy.h"
#include "yb/tserver/tserver_shared_mem.h"

#include "yb/util/memory/arena.h"
#include "yb/util/memory/mc_types.h"
#include "yb/util/result.h"
#include "yb/util/status_log.h"

#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pggate/ybc_pggate.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

using std::string;

using namespace std::literals;

DECLARE_string(pggate_master_addresses);
DECLARE_string(test_leave_files);
DECLARE_bool(enable_object_locking_for_table_locks);

namespace yb {
namespace pggate {
namespace {

YbcPgMemctx global_test_memctx = nullptr;
YbcAshMetadata ash_metadata;
YbcPgAshConfig ash_config;
YbcPgSharedDataPlaceholder shared_data_placeholder;

YbcPgMemctx GetCurrentTestYbMemctx() {
  if (!global_test_memctx) {
    global_test_memctx = YBCPgCreateMemctx();
  }
  return global_test_memctx;
}

void ClearCurrentTestYbMemctx() {
  if (global_test_memctx != nullptr) {
    CHECK_YBC_STATUS(YBCPgDestroyMemctx(global_test_memctx));

    // We assume the memory context has actually already been deleted.
    global_test_memctx = nullptr;
  }
}

const char* GetDebugQueryStringStub() {
  return "GetDebugQueryString not implemented in test";
}

YbcWaitEventInfo PgstatReportWaitStartNoOp(YbcWaitEventInfo info) {
  return info;
}

YbcReadPointHandle GetCatalogSnapshotReadPoint(YbcPgOid table_oid, bool create_if_not_exists) {
  return 0;
}

uint16_t GetSessionReplicationOriginId() {
  return 0;
}

void CheckForInterruptsNoOp() {
}

} // namespace

PggateTest::PggateTest() = default;

PggateTest::~PggateTest() = default;

//--------------------------------------------------------------------------------------------------
// Error handling routines.
void PggateTest::CheckYBCStatus(YbcStatus status, const char* file_name, int line_number) {
  CHECK_OK(Status(status, AddRef::kTrue));
}

void *PggateTestAlloc(size_t bytes) {
  static MemoryContext memctx;
  return static_cast<void*>(memctx.AllocateBytes(bytes));
}

// This implementation is different from what PostgreSQL's cstring_to_text_with_len function does.
// Here we just copy the given string and add a terminating zero. This is what our test expects.
struct varlena* PggateTestCStringToTextWithLen(const char* c, int size) {
  static MemoryContext memctx;
  CHECK_GE(size, 0);
  CHECK_LE(size, 1024ll * 1024 * 1024 - 4);

  char* buf = static_cast<char*>(memctx.AllocateBytes(size + 1));
  memcpy(buf, c, size);
  buf[size] = 0;
  return reinterpret_cast<struct varlena*>(buf);
}

void *PggateTestSwitchMemoryContext(void* context) {
  return context;
}

void *PggateTestCreateMemContext(void* parent, const char*name) {
  static MemoryContext memctx;
  return static_cast<void*>(&memctx);
}

void PggateTestDeleteMemContext(void* context) {
}

//--------------------------------------------------------------------------------------------------
// Starting and ending routines.

void PggateTest::SetUp() {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_test_leave_files) = "always";
  // The tests by pass creating pg connections and perform custom operations by creating
  // YbcPgStatement and invoking YBCPgExecInsert/YBCPgExecSelect etc. In the normal case,
  // these functions are invoked from the ysql backend after opening the relevant relations
  // (and thus acquiring relevant object locks). As a result, the sanity check which ensures
  // that some object locks are taken before performing a read/write fails for these tests.
  // Hence, disabling the object locks feature for this suite.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_object_locking_for_table_locks) = false;
  YBTest::SetUp();
}

void PggateTest::TearDown() {
  // It is important to destroy the memory context before destroying PgGate.
  ClearCurrentTestYbMemctx();

  // Destroy the client before shutting down servers.
  YBCDestroyPgGate();

  // Destroy all servers.
  if (cluster_ != nullptr) {
    cluster_->Shutdown();
    cluster_ = nullptr;
  }
  YBTest::TearDown();
}

Status PggateTest::Init(
    const char *test_name, int num_tablet_servers, int replication_factor, bool should_create_db) {
  // Create cluster before setting client API.
  RETURN_NOT_OK(CreateCluster(num_tablet_servers, replication_factor));

  // Init PgGate API.
  CHECK_YBC_STATUS(YBCInit(test_name, PggateTestAlloc, PggateTestCStringToTextWithLen,
                           PggateTestSwitchMemoryContext, PggateTestCreateMemContext,
                           PggateTestDeleteMemContext));

  YbcPgCallbacks callbacks;

  auto* session_stats =
      static_cast<YbcPgExecStatsState*>(PggateTestAlloc(sizeof(YbcPgExecStatsState)));
  memset(session_stats, 0, sizeof(YbcPgExecStatsState));
  callbacks.GetCurrentYbMemctx = &GetCurrentTestYbMemctx;
  callbacks.GetDebugQueryString = &GetDebugQueryStringStub;
  callbacks.PgstatReportWaitStart = &PgstatReportWaitStartNoOp;
  callbacks.GetCatalogSnapshotReadPoint = &GetCatalogSnapshotReadPoint;
  callbacks.GetSessionReplicationOriginId = &GetSessionReplicationOriginId;
  callbacks.CheckForInterrupts = &CheckForInterruptsNoOp;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_pggate_tserver_shared_memory_uuid) =
      cluster_->tablet_server(0)->instance_id().permanent_uuid();

  ash_metadata.query_id = 5; // to make sure a DCHECK passes during metadata serialization
  ash_config.metadata = &ash_metadata;

  YbcPgInitPostgresInfo init_info{
    .parallel_leader_session_id = nullptr,
    .shared_data = &shared_data_placeholder};
  CHECK_YBC_STATUS(YBCInitPgGate(
      YBCTestGetTypeTable(), &callbacks, &init_info, &ash_config, session_stats,
      false /* is_binary_upgrade */));
  if (should_create_db) {
    CreateDB();
  }

  return Status::OK();
}

Status PggateTest::CreateCluster(int num_tablet_servers, int replication_factor) {
  // Start mini-cluster with given number of tservers (default: 3).
  ExternalMiniClusterOptions opts;
  opts.num_tablet_servers = num_tablet_servers;
  opts.data_root_counter = 0;
  if (replication_factor > 0) {
    opts.replication_factor = replication_factor;
  }
  CustomizeExternalMiniCluster(&opts);
  cluster_ = std::make_shared<ExternalMiniCluster>(opts);
  CHECK_OK(cluster_->Start());

  // Setup master address to construct YBClient.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_pggate_master_addresses) = cluster_->GetMasterAddresses();

  // Sleep to make sure the cluster is ready before accepting client messages.
  sleep(1);
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------


void PggateTest::CreateDB() {
  YbcPgStatement pg_stmt;
  CHECK_YBC_STATUS(YBCPgNewCreateDatabase(
      kDefaultDatabase, kDefaultDatabaseOid, 0 /* source_database_oid */,
      0 /* next_oid */, false /* colocated */, NULL /* yb_clone_info */, &pg_stmt));
  CHECK_YBC_STATUS(YBCPgExecCreateDatabase(pg_stmt));
}

void PggateTest::BeginDDLTransaction() {
  CHECK_YBC_STATUS(YBCPgEnterSeparateDdlTxnMode());
}

void PggateTest::CommitDDLTransaction() {
  CHECK_YBC_STATUS(YBCPgExitSeparateDdlTxnMode(0 /* db_oid */, false /* is_silent_altering */));
  // Next reads from catalog tables have to see changes made by the DDL transaction.
  YBCPgResetCatalogReadTime();
}

void PggateTest::BeginTransaction() {
  CHECK_YBC_STATUS(YBCPgBeginTransaction(0));
}

void PggateTest::CommitTransaction() {
  CHECK_YBC_STATUS(YBCPgCommitPlainTransaction());
}

void PggateTest::ExecCreateTableTransaction(YbcPgStatement pg_stmt) {
  BeginDDLTransaction();
  CHECK_YBC_STATUS(YBCPgExecCreateTable(pg_stmt));
  CommitDDLTransaction();
}

Result<pgwrapper::PGConn> PggateTest::PgConnect(const std::string& database_name) {
  auto* ts = cluster_->tablet_server(0);
  return pgwrapper::PGConnBuilder({
                                      .host = ts->bind_host(),
                                      .port = ts->pgsql_rpc_port(),
                                      .dbname = database_name,
                                  })
      .Connect();
}

// ------------------------------------------------------------------------------------------------
// Make sure that DataType in common.proto matches the YbcPgDataType enum
// TODO: find a better way to generate these enums.

static_assert(static_cast<int>(PersistentDataType::UNKNOWN_DATA) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_UNKNOWN_DATA),
              "DataType::UNKNOWN_DATA does not match YbcPgDataType::UNKNOWN_DATA");

static_assert(static_cast<int>(PersistentDataType::NULL_VALUE_TYPE) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_NULL_VALUE_TYPE),
              "DataType::NULL_VALUE_TYPE does not match YbcPgDataType::NULL_VALUE_TYPE");

static_assert(static_cast<int>(PersistentDataType::INT8) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_INT8),
              "DataType::INT8 does not match YbcPgDataType::INT8");

static_assert(static_cast<int>(PersistentDataType::INT16) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_INT16),
              "DataType::INT16 does not match YbcPgDataType::INT16");

static_assert(static_cast<int>(PersistentDataType::INT32) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_INT32),
              "DataType::INT32 does not match YbcPgDataType::INT32");

static_assert(static_cast<int>(PersistentDataType::INT64) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_INT64),
              "DataType::INT64 does not match YbcPgDataType::INT64");

static_assert(static_cast<int>(PersistentDataType::STRING) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_STRING),
              "DataType::STRING does not match YbcPgDataType::STRING");

static_assert(static_cast<int>(PersistentDataType::BOOL) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_BOOL),
              "DataType::BOOL does not match YbcPgDataType::BOOL");

static_assert(static_cast<int>(PersistentDataType::FLOAT) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_FLOAT),
              "DataType::FLOAT does not match YbcPgDataType::FLOAT");

static_assert(static_cast<int>(PersistentDataType::DOUBLE) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_DOUBLE),
              "DataType::DOUBLE does not match YbcPgDataType::DOUBLE");

static_assert(static_cast<int>(PersistentDataType::BINARY) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_BINARY),
              "DataType::BINARY does not match YbcPgDataType::BINARY");

static_assert(static_cast<int>(PersistentDataType::TIMESTAMP) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_TIMESTAMP),
              "DataType::TIMESTAMP does not match YbcPgDataType::TIMESTAMP");

static_assert(static_cast<int>(PersistentDataType::DECIMAL) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_DECIMAL),
              "DataType::DECIMAL does not match YbcPgDataType::DECIMAL");

static_assert(static_cast<int>(PersistentDataType::VARINT) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_VARINT),
              "DataType::VARINT does not match YbcPgDataType::VARINT");

static_assert(static_cast<int>(PersistentDataType::INET) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_INET),
              "DataType::INET does not match YbcPgDataType::INET");

static_assert(static_cast<int>(PersistentDataType::LIST) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_LIST),
              "DataType::LIST does not match YbcPgDataType::LIST");

static_assert(static_cast<int>(PersistentDataType::MAP) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_MAP),
              "DataType::MAP does not match YbcPgDataType::MAP");

static_assert(static_cast<int>(PersistentDataType::SET) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_SET),
              "DataType::SET does not match YbcPgDataType::SET");

static_assert(static_cast<int>(PersistentDataType::UUID) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_UUID),
              "DataType::UUID does not match YbcPgDataType::UUID");

static_assert(static_cast<int>(PersistentDataType::TIMEUUID) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_TIMEUUID),
              "DataType::TIMEUUID does not match YbcPgDataType::TIMEUUID");

static_assert(static_cast<int>(PersistentDataType::TUPLE) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_TUPLE),
              "DataType::TUPLE does not match YbcPgDataType::TUPLE");

static_assert(static_cast<int>(PersistentDataType::TYPEARGS) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_TYPEARGS),
              "DataType::TYPEARGS does not match YbcPgDataType::TYPEARGS");

static_assert(static_cast<int>(PersistentDataType::USER_DEFINED_TYPE) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_USER_DEFINED_TYPE),
              "DataType::USER_DEFINED_TYPE does not match YbcPgDataType::USER_DEFINED_TYPE");

static_assert(static_cast<int>(PersistentDataType::FROZEN) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_FROZEN),
              "DataType::FROZEN does not match YbcPgDataType::FROZEN");

static_assert(static_cast<int>(PersistentDataType::DATE) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_DATE),
              "DataType::DATE does not match YbcPgDataType::DATE");

static_assert(static_cast<int>(PersistentDataType::TIME) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_TIME),
              "DataType::TIME does not match YbcPgDataType::TIME");

static_assert(static_cast<int>(PersistentDataType::JSONB) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_JSONB),
              "DataType::JSONB does not match YbcPgDataType::JSONB");

static_assert(static_cast<int>(PersistentDataType::UINT8) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_UINT8),
              "DataType::UINT8 does not match YbcPgDataType::UINT8");

static_assert(static_cast<int>(PersistentDataType::UINT16) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_UINT16),
              "DataType::UINT16 does not match YbcPgDataType::UINT16");

static_assert(static_cast<int>(PersistentDataType::UINT32) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_UINT32),
              "DataType::UINT32 does not match YbcPgDataType::UINT32");

static_assert(static_cast<int>(PersistentDataType::UINT64) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_UINT64),
              "DataType::UINT64 does not match YbcPgDataType::UINT64");

static_assert(static_cast<int>(PersistentDataType::VECTOR) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_VECTOR),
              "DataType::VECTOR does not match YbcPgDataType::VECTOR");

static_assert(static_cast<int>(PersistentDataType::BSON) ==
                  static_cast<int>(YbcPgDataType::YB_YQL_DATA_TYPE_BSON),
              "DataType::BSON does not match YbcPgDataType::BSON");

// End of data type enum consistency checking
// ------------------------------------------------------------------------------------------------

} // namespace pggate
} // namespace yb
