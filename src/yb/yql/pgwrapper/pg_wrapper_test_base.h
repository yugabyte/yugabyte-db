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

#pragma once

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

namespace yb {
namespace pgwrapper {

class PgWrapperTestBase : public MiniClusterTestWithClient<ExternalMiniCluster> {
 protected:
  void SetUp() override;

  virtual int GetNumMasters() const { return 1; }

  virtual int GetNumTabletServers() const {
    // Test that we can start PostgreSQL servers on non-colliding ports within each tablet server.
    return 3;
  }

  virtual void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) {}

  Result<TabletId> GetSingleTabletId(const TableName& table_name);

  Result<std::string> RunYbAdminCommand(const std::string& cmd);

  // Tablet server to use to perform PostgreSQL operations.
  ExternalTabletServer* pg_ts = nullptr;
};

class PgCommandTestBase : public PgWrapperTestBase {
 protected:
  PgCommandTestBase(bool auth, bool encrypted)
      : use_auth_(auth), encrypt_connection_(encrypted) {}

  std::string GetDbName() {
    return db_name_;
  }

  void SetDbName(const std::string& db_name) {
    db_name_ = db_name;
  }

  YB_STRONGLY_TYPED_BOOL(TuplesOnly);

  Result<std::string> RunPsqlCommand(
      const std::string &statement, TuplesOnly tuples_only = TuplesOnly::kFalse);

  void RunPsqlCommand(
      const std::string &statement, const std::string &expected_output, bool tuples_only = false);

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override;

  void CreateTable(const std::string &statement) {
    RunPsqlCommand(statement, "CREATE TABLE");
  }

  void CreateType(const std::string &statement) {
    RunPsqlCommand(statement, "CREATE TYPE");
  }

  void CreateSequence(const std::string &statement) {
    RunPsqlCommand(statement, "CREATE SEQUENCE");
  }

  void CreateIndex(const std::string &statement) {
    RunPsqlCommand(statement, "CREATE INDEX");
  }

  void CreateView(const std::string &statement) {
    RunPsqlCommand(statement, "CREATE VIEW");
  }

  void CreateProcedure(const std::string &statement) {
    RunPsqlCommand(statement, "CREATE PROCEDURE");
  }

  void CreateSchema(const std::string &statement) {
    RunPsqlCommand(statement, "CREATE SCHEMA");
  }

  void Call(const std::string &statement) {
    RunPsqlCommand(statement, "CALL");
  }

  void InsertRows(const std::string& statement, size_t expected_rows) {
    RunPsqlCommand(statement, Format("INSERT 0 $0", expected_rows));
  }

  void InsertOneRow(const std::string& statement) {
    InsertRows(statement, /* expected_rows */ 1);
  }

  void UpdateOneRow(const std::string &statement) {
    RunPsqlCommand(statement, "UPDATE 1");
  }

 private:
  const bool use_auth_;
  const bool encrypt_connection_;

  std::string db_name_;
};

} // namespace pgwrapper
} // namespace yb
