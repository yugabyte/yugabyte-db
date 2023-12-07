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

using std::string;

using namespace std::literals;

DECLARE_string(pggate_master_addresses);
DECLARE_string(test_leave_files);

namespace yb {
namespace pggate {
namespace {

YBCPgMemctx global_test_memctx = nullptr;

YBCPgMemctx GetCurrentTestYbMemctx() {
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

} // namespace

PggateTest::PggateTest()
    : tserver_shared_object_(CHECK_RESULT(tserver::TServerSharedObject::Create())) {
}

PggateTest::~PggateTest() {
}

//--------------------------------------------------------------------------------------------------
// Error handling routines.
void PggateTest::CheckYBCStatus(YBCStatus status, const char* file_name, int line_number) {
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

//--------------------------------------------------------------------------------------------------
// Starting and ending routines.

void PggateTest::SetUp() {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_test_leave_files) = "always";
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

Status PggateTest::Init(const char *test_name,
                        int num_tablet_servers,
                        int replication_factor,
                        const std::string& use_existing_db) {
  // Create cluster before setting client API.
  RETURN_NOT_OK(CreateCluster(num_tablet_servers, replication_factor));

  // Init PgGate API.
  CHECK_YBC_STATUS(YBCInit(test_name, PggateTestAlloc, PggateTestCStringToTextWithLen));

  const YBCPgTypeEntity *type_table = nullptr;
  int count = 0;
  YBCTestGetTypeTable(&type_table, &count);
  YBCPgCallbacks callbacks;
  auto* session_stats =
      static_cast<YBCPgExecStatsState*>(PggateTestAlloc(sizeof(YBCPgExecStatsState)));
  memset(session_stats, 0, sizeof(YBCPgExecStatsState));
  callbacks.GetCurrentYbMemctx = &GetCurrentTestYbMemctx;
  callbacks.GetDebugQueryString = &GetDebugQueryStringStub;

  {
    auto proxy = cluster_->GetProxy<tserver::TabletServerServiceProxy>(cluster_->tablet_server(0));
    tserver::GetSharedDataRequestPB req;
    tserver::GetSharedDataResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(30s);
    CHECK_OK(proxy.GetSharedData(req, &resp, &controller));
    CHECK_EQ(resp.data().size(), sizeof(*tserver_shared_object_));
    memcpy(pointer_cast<char*>(&*tserver_shared_object_), resp.data().c_str(), resp.data().size());
  }
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_pggate_tserver_shm_fd) = tserver_shared_object_.GetFd();

  YBCInitPgGate(type_table, count, callbacks, nullptr, nullptr);

  // Setup session.
  CHECK_YBC_STATUS(YBCPgInitSession(nullptr /* database_name */, session_stats));

  if (use_existing_db.empty()) {
    // Setup database
    SetupDB();
  } else {
    ConnectDB(use_existing_db);
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

void PggateTest::SetupDB(const string& db_name, const YBCPgOid db_oid) {
  CreateDB(db_name, db_oid);
  ConnectDB(db_name);
}

void PggateTest::CreateDB(const string& db_name, const YBCPgOid db_oid) {
  YBCPgStatement pg_stmt;
  CHECK_YBC_STATUS(YBCPgNewCreateDatabase(
      db_name.c_str(), db_oid, 0 /* source_database_oid */, 0 /* next_oid */, false /* colocated */,
      &pg_stmt));
  CHECK_YBC_STATUS(YBCPgExecCreateDatabase(pg_stmt));
}

void PggateTest::ConnectDB(const string& db_name) {
  CHECK_YBC_STATUS(YBCPgConnectDatabase(db_name.c_str()));
}

void PggateTest::BeginDDLTransaction() {
  CHECK_YBC_STATUS(YBCPgEnterSeparateDdlTxnMode());
}

void PggateTest::CommitDDLTransaction() {
  CHECK_YBC_STATUS(YBCPgExitSeparateDdlTxnMode(0 /* db_oid */, false /* is_silent_altering */));
}

void PggateTest::BeginTransaction() {
  CHECK_YBC_STATUS(YBCPgBeginTransaction(0));
}

void PggateTest::CommitTransaction() {
  CHECK_YBC_STATUS(YBCPgCommitTransaction());
}

void PggateTest::ExecCreateTableTransaction(YBCPgStatement pg_stmt) {
  BeginDDLTransaction();
  CHECK_YBC_STATUS(YBCPgExecCreateTable(pg_stmt));
  CommitDDLTransaction();
}

// ------------------------------------------------------------------------------------------------
// Make sure that DataType in common.proto matches the YBCPgDataType enum
// TODO: find a better way to generate these enums.

static_assert(static_cast<int>(PersistentDataType::UNKNOWN_DATA) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_UNKNOWN_DATA),
              "DataType::UNKNOWN_DATA does not match YBCPgDataType::UNKNOWN_DATA");

static_assert(static_cast<int>(PersistentDataType::NULL_VALUE_TYPE) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_NULL_VALUE_TYPE),
              "DataType::NULL_VALUE_TYPE does not match YBCPgDataType::NULL_VALUE_TYPE");

static_assert(static_cast<int>(PersistentDataType::INT8) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_INT8),
              "DataType::INT8 does not match YBCPgDataType::INT8");

static_assert(static_cast<int>(PersistentDataType::INT16) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_INT16),
              "DataType::INT16 does not match YBCPgDataType::INT16");

static_assert(static_cast<int>(PersistentDataType::INT32) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_INT32),
              "DataType::INT32 does not match YBCPgDataType::INT32");

static_assert(static_cast<int>(PersistentDataType::INT64) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_INT64),
              "DataType::INT64 does not match YBCPgDataType::INT64");

static_assert(static_cast<int>(PersistentDataType::STRING) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_STRING),
              "DataType::STRING does not match YBCPgDataType::STRING");

static_assert(static_cast<int>(PersistentDataType::BOOL) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_BOOL),
              "DataType::BOOL does not match YBCPgDataType::BOOL");

static_assert(static_cast<int>(PersistentDataType::FLOAT) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_FLOAT),
              "DataType::FLOAT does not match YBCPgDataType::FLOAT");

static_assert(static_cast<int>(PersistentDataType::DOUBLE) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_DOUBLE),
              "DataType::DOUBLE does not match YBCPgDataType::DOUBLE");

static_assert(static_cast<int>(PersistentDataType::BINARY) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_BINARY),
              "DataType::BINARY does not match YBCPgDataType::BINARY");

static_assert(static_cast<int>(PersistentDataType::TIMESTAMP) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_TIMESTAMP),
              "DataType::TIMESTAMP does not match YBCPgDataType::TIMESTAMP");

static_assert(static_cast<int>(PersistentDataType::DECIMAL) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_DECIMAL),
              "DataType::DECIMAL does not match YBCPgDataType::DECIMAL");

static_assert(static_cast<int>(PersistentDataType::VARINT) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_VARINT),
              "DataType::VARINT does not match YBCPgDataType::VARINT");

static_assert(static_cast<int>(PersistentDataType::INET) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_INET),
              "DataType::INET does not match YBCPgDataType::INET");

static_assert(static_cast<int>(PersistentDataType::LIST) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_LIST),
              "DataType::LIST does not match YBCPgDataType::LIST");

static_assert(static_cast<int>(PersistentDataType::MAP) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_MAP),
              "DataType::MAP does not match YBCPgDataType::MAP");

static_assert(static_cast<int>(PersistentDataType::SET) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_SET),
              "DataType::SET does not match YBCPgDataType::SET");

static_assert(static_cast<int>(PersistentDataType::UUID) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_UUID),
              "DataType::UUID does not match YBCPgDataType::UUID");

static_assert(static_cast<int>(PersistentDataType::TIMEUUID) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_TIMEUUID),
              "DataType::TIMEUUID does not match YBCPgDataType::TIMEUUID");

static_assert(static_cast<int>(PersistentDataType::TUPLE) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_TUPLE),
              "DataType::TUPLE does not match YBCPgDataType::TUPLE");

static_assert(static_cast<int>(PersistentDataType::TYPEARGS) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_TYPEARGS),
              "DataType::TYPEARGS does not match YBCPgDataType::TYPEARGS");

static_assert(static_cast<int>(PersistentDataType::USER_DEFINED_TYPE) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_USER_DEFINED_TYPE),
              "DataType::USER_DEFINED_TYPE does not match YBCPgDataType::USER_DEFINED_TYPE");

static_assert(static_cast<int>(PersistentDataType::FROZEN) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_FROZEN),
              "DataType::FROZEN does not match YBCPgDataType::FROZEN");

static_assert(static_cast<int>(PersistentDataType::DATE) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_DATE),
              "DataType::DATE does not match YBCPgDataType::DATE");

static_assert(static_cast<int>(PersistentDataType::TIME) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_TIME),
              "DataType::TIME does not match YBCPgDataType::TIME");

static_assert(static_cast<int>(PersistentDataType::JSONB) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_JSONB),
              "DataType::JSONB does not match YBCPgDataType::JSONB");

static_assert(static_cast<int>(PersistentDataType::UINT8) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_UINT8),
              "DataType::UINT8 does not match YBCPgDataType::UINT8");

static_assert(static_cast<int>(PersistentDataType::UINT16) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_UINT16),
              "DataType::UINT16 does not match YBCPgDataType::UINT16");

static_assert(static_cast<int>(PersistentDataType::UINT32) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_UINT32),
              "DataType::UINT32 does not match YBCPgDataType::UINT32");

static_assert(static_cast<int>(PersistentDataType::UINT64) ==
                  static_cast<int>(YBCPgDataType::YB_YQL_DATA_TYPE_UINT64),
              "DataType::UINT64 does not match YBCPgDataType::UINT64");

// End of data type enum consistency checking
// ------------------------------------------------------------------------------------------------

} // namespace pggate
} // namespace yb
