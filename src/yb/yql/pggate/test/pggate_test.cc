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
#include <gflags/gflags.h>

DECLARE_string(pggate_master_addresses);
DECLARE_string(test_leave_files);

namespace yb {
namespace pggate {

PggateTest::PggateTest() {
}

PggateTest::~PggateTest() {
}

//--------------------------------------------------------------------------------------------------
// Error handling routines.
void PggateTest::CheckYBCStatus(YBCStatus status, const char* file_name, int line_number) {
  if (!status) {
    return;
  }

  auto code = static_cast<Status::Code>(status->code);
  Status s(code, file_name, line_number, status->msg);
  CHECK_OK(s);
}

void *PggateTestAlloc(size_t bytes) {
  static MemoryContext memctx;
  return static_cast<void*>(memctx.AllocateBytes(bytes));
}

//--------------------------------------------------------------------------------------------------
// Starting and ending routines.
void PggateTest::SetUp() {
  FLAGS_test_leave_files = "always";
  YBTest::SetUp();
}

void PggateTest::TearDown() {
  // Destroy the client before shutting down servers.
  YBCDestroyPgGate();

  // Destroy all servers.
  if (cluster_ != nullptr) {
    cluster_->Shutdown();
    cluster_ = nullptr;
  }
  YBTest::TearDown();
}

Status PggateTest::Init(const char *test_name, int num_tablet_servers) {
  // Create cluster before setting client API.
  RETURN_NOT_OK(CreateCluster(num_tablet_servers));

  // Init PgGate API.
  CHECK_YBC_STATUS(YBCInit(test_name, PggateTestAlloc));
  YBCInitPgGate();

  // Setup session.
  CHECK_YBC_STATUS(YBCPgCreateSession(nullptr, "", &pg_session_));

  // Setup database
  SetupDB();
  return Status::OK();
}

CHECKED_STATUS PggateTest::CreateCluster(int num_tablet_servers) {
  // Start mini-cluster with given number of tservers (default: 3).
  ExternalMiniClusterOptions opts;
  opts.num_tablet_servers = num_tablet_servers;
  opts.data_root_counter = 0;
  cluster_ = std::make_shared<ExternalMiniCluster>(opts);
  CHECK_OK(cluster_->Start());

  // Setup master address to construct YBClient.
  FLAGS_pggate_master_addresses = cluster_->GetMasterAddresses();

  // Sleep to make sure the cluster is ready before accepting client messages.
  sleep(1);
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

void PggateTest::SetupDB(const string& db_name) {
  CreateDB(db_name);
  ConnectDB(db_name);
}

void PggateTest::CreateDB(const string& db_name) {
  YBCPgStatement pg_stmt;
  CHECK_YBC_STATUS(YBCPgAllocCreateDatabase(pg_session_, db_name.c_str(), &pg_stmt));
  CHECK_YBC_STATUS(YBCPgExecCreateDatabase(pg_stmt));
}

void PggateTest::ConnectDB(const string& db_name) {
  CHECK_YBC_STATUS(YBCPgConnectDatabase(pg_session_, db_name.c_str()));
}

} // namespace pggate
} // namespace yb
