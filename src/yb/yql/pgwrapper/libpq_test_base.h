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

#pragma once

#include "yb/util/monotime.h"
#include "yb/util/tostring.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper_test_base.h"

namespace yb {
namespace pgwrapper {

struct YsqlMetric {
  std::string name;
  std::unordered_map<std::string, std::string> labels;
  int64_t value;
  int64_t time;
  std::string type;
  std::string description;

  YsqlMetric(
      std::string name, std::unordered_map<std::string, std::string> labels, int64_t value,
      int64_t time, std::string type = "", std::string description = "")
      : name(std::move(name)),
        labels(std::move(labels)),
        value(value),
        time(time),
        type(type),
        description(description) {}
};

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
  Result<PGConn> ConnectToTsForDB(const ExternalTabletServer& pg_ts, const std::string& db_name);
  Result<PGConn> ConnectToTsAsUser(
      const ExternalTabletServer& pg_ts,
      const std::string& user);
  Result<PGConn> ConnectUsingString(
      const std::string& conn_str,
      CoarseTimePoint deadline = CoarseMonoClock::Now() + MonoDelta::FromSeconds(10),
      bool simple_query_protocol = false);
  Result<PGConn> ConnectToDBWithReplication(const std::string& db_name);
  void SerializableColoringHelper(int min_duration_seconds = 0);
  static bool TransactionalFailure(const Status& status);
  static Status BumpCatalogVersion(int num_bumps, PGConn* conn,
                                   const std::string& alter_value = "");
  static void UpdateMiniClusterFailOnConflict(ExternalMiniClusterOptions* options);
  static std::vector<YsqlMetric> ParsePrometheusMetrics(const std::string& metrics_output);
  static std::vector<YsqlMetric> ParseJsonMetrics(const std::string& metrics_output);
  std::vector<YsqlMetric> GetJsonMetrics();
  std::vector<YsqlMetric> GetPrometheusMetrics();
  static int64_t GetMetricValue(
      const std::vector<YsqlMetric>& metrics,
      const std::string& metric_name);
  static void WaitForCatalogVersionToPropagate();
  Result<int64_t> GetCatCacheTableMissMetric(const std::string& table_name);
};

Result<PgOid> GetDatabaseOid(PGConn* conn, const std::string& db_name);
Result<std::string> GetPGVersionString(PGConn* conn);

} // namespace pgwrapper
} // namespace yb
