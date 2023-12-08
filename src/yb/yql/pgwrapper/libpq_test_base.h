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

#include "yb/util/monotime.h"
#include "yb/util/tostring.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper_test_base.h"

namespace yb {
namespace pgwrapper {

class LibPqTestBase : public PgWrapperTestBase {
 protected:
  void SetUp() override;
  Result<PGConn> Connect(bool simple_query_protocol = false);
  Result<PGConn> ConnectToDB(const std::string& db_name, bool simple_query_protocol = false);
  Result<PGConn> ConnectToDBAsUser(
      const std::string& db_name,
      const std::string& user,
      bool simple_query_protocol = false);
  Result<PGConn> ConnectToTs(const ExternalTabletServer& pg_ts);
  Result<PGConn> ConnectUsingString(
      const std::string& conn_str,
      CoarseTimePoint deadline = CoarseMonoClock::Now() + MonoDelta::FromSeconds(10),
      bool simple_query_protocol = false);
  void SerializableColoringHelper(int min_duration_seconds = 0);
  static bool TransactionalFailure(const Status& status);
  static void BumpCatalogVersion(int num_versions, PGConn* conn);
  static void UpdateMiniClusterFailOnConflict(ExternalMiniClusterOptions* options);
};

Result<PgOid> GetDatabaseOid(PGConn* conn, const std::string& db_name);

} // namespace pgwrapper
} // namespace yb
