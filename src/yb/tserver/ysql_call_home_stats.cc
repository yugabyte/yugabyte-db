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

#include "yb/tserver/ysql_call_home_stats.h"

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "yb/tserver/tablet_server_interface.h"

#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

// =============================================================================
// YSQL Statistics Query Definitions
// =============================================================================
// Queries are organized into three levels:
//
// 1. CLUSTER LEVEL - Run once from any database (queries on shared catalog tables).
//    Collected by: Master leader via RPC to the closest TServer.
//
// 2. DB LEVEL - Run for each database (per-db catalogs).
//    Collected by: Master leader via RPC to the closest TServer.
//
// 3. NODE LEVEL - Run once, covers all databases (node-local stats).
//    Collected by: Each TServer locally.
//
// HOW TO ADD NEW QUERIES:
// -----------------------
// 1. Add a new entry to the appropriate array in ysql_call_home_stats.h.
// 2. Write a simple SELECT query with column aliases.
// 3. Column aliases become JSON keys automatically.
// 4. The framework wraps your query as:
//      SELECT COALESCE(json_agg(row_to_json(t)), '[]'::json) FROM (<your_query>) t
//    This converts each row to a JSON object (using column aliases as keys),
//    aggregates all rows into a JSON array, and returns '[]' if the result is empty.
//
// PRIVACY: Queries must only read aggregate metadata from system catalogs (e.g. pg_database,
// pg_extension, pg_stat_activity). Never collect user data, query text, row contents, passwords,
// or any personally identifiable information. The call home payload is sent to an external
// diagnostics endpoint.
//
// Example:
//   {"tablespaces", "SELECT COUNT(*) AS count FROM pg_tablespace"}
//   Wrapped query:
//     SELECT COALESCE(json_agg(row_to_json(t)), '[]'::json)
//     FROM (SELECT COUNT(*) AS count FROM pg_tablespace) t
//   Output: [{"count": 3}]

DEFINE_RUNTIME_int32(callhome_ysql_connect_timeout_ms, 30000,
    "Timeout in milliseconds for establishing a PostgreSQL connection during YSQL call-home "
    "collection. Set to 0 for no timeout.");

DEFINE_RUNTIME_int32(callhome_ysql_statement_timeout_ms, 60000,
    "Timeout in milliseconds for individual query execution during YSQL call-home collection. "
    "Passed directly to PostgreSQL's statement_timeout GUC. Set to 0 for no timeout.");

using std::string;

namespace yb {
namespace tserver {

namespace {

template <class TValue>
TValue ResultToValueWithWarning(
    Result<TValue> result, TValue default_value, const string& error_message) {
  if (!result.ok()) {
    LOG(WARNING) << "YSQL Call Home Stats: " << error_message << ": " << result.status();
  }
  return ResultToValue(std::move(result), std::move(default_value));
}

std::optional<CoarseTimePoint> ConnectionDeadline() {
  if (FLAGS_callhome_ysql_connect_timeout_ms > 0) {
    return CoarseMonoClock::Now() +
           MonoDelta::FromMilliseconds(FLAGS_callhome_ysql_connect_timeout_ms);
  }
  return std::nullopt;
}

Result<pgwrapper::PGConn> ConnectToDb(TabletServerIf* server, const string& dbname) {
  auto conn = VERIFY_RESULT(
      server->CreateInternalPGConn(dbname, false, ConnectionDeadline()));

  if (FLAGS_callhome_ysql_statement_timeout_ms > 0) {
    RETURN_NOT_OK(conn.ExecuteFormat(
        "SET statement_timeout = $0", FLAGS_callhome_ysql_statement_timeout_ms));
  }
  RETURN_NOT_OK(conn.Execute("SET default_transaction_read_only = true"));
  RETURN_NOT_OK(conn.Execute("SET application_name = 'ysql_callhome'"));

  return conn;
}

string WrapQueryAsJson(std::string_view query) {
  return Format("SELECT COALESCE(json_agg(row_to_json(t)), '[]'::json) FROM ($0) t", query);
}

Result<string> ExecuteQuery(pgwrapper::PGConn& conn, std::string_view query) {
  return VERIFY_RESULT(conn.FetchRow<std::string>(WrapQueryAsJson(query)));
}

Result<string> ExecuteQueries(
    pgwrapper::PGConn& conn, std::span<const YsqlCallHomeQuery> queries) {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  writer.StartObject();
  for (const auto& [key, query] : queries) {
    auto value = ResultToValueWithWarning(
        ExecuteQuery(conn, query), string("[]"), Format("Query '$0' failed", key));
    writer.Key(key.data(), static_cast<rapidjson::SizeType>(key.size()));
    writer.RawValue(value.c_str(), value.size(), rapidjson::kArrayType);
  }
  writer.EndObject();
  return buffer.GetString();
}

Result<std::vector<string>> GetDbs(TabletServerIf* server) {
  auto conn = VERIFY_RESULT(ConnectToDb(server, "template1"));
  return conn.FetchRows<std::string>(
      "SELECT datname FROM pg_database "
      "WHERE datistemplate = false AND datallowconn = true "
      "ORDER BY datname");
}

Result<string> CollectDbStats(
    TabletServerIf* server, const string& dbname,
    std::span<const YsqlCallHomeQuery> queries) {
  auto conn = VERIFY_RESULT(ConnectToDb(server, dbname));
  return ExecuteQueries(conn, queries);
}

string CollectAggregateStats(
    TabletServerIf* server, const std::vector<string>& databases,
    std::span<const YsqlCallHomeQuery> aggregate_queries) {
  if (databases.empty()) {
    return "{}";
  }
  return ResultToValueWithWarning(
      CollectDbStats(server, databases[0], aggregate_queries), string("{}"),
      "Failed to collect aggregate stats");
}

string CollectPerDbStats(
    TabletServerIf* server, const std::vector<string>& databases,
    std::span<const YsqlCallHomeQuery> per_db_queries) {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  writer.StartArray();
  for (const auto& dbname : databases) {
    auto stats = ResultToValueWithWarning(
        CollectDbStats(server, dbname, per_db_queries), string("{}"),
        Format("Failed to collect stats for '$0'", dbname));
    writer.StartObject();
    writer.Key("name");
    writer.String(dbname.c_str(), static_cast<rapidjson::SizeType>(dbname.size()));
    writer.Key("stats");
    writer.RawValue(stats.c_str(), stats.size(), rapidjson::kObjectType);
    writer.EndObject();
  }
  writer.EndArray();
  return buffer.GetString();
}

}  // anonymous namespace

string BuildStatsJson(
    TabletServerIf* server, const std::vector<string>& databases,
    std::span<const YsqlCallHomeQuery> aggregate_queries,
    std::span<const YsqlCallHomeQuery> per_db_queries) {
  auto aggregate_stats = CollectAggregateStats(server, databases, aggregate_queries);

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  writer.StartObject();
  writer.Key("aggregate");
  writer.RawValue(aggregate_stats.c_str(), aggregate_stats.size(), rapidjson::kObjectType);
  if (!per_db_queries.empty()) {
    auto per_db_stats = CollectPerDbStats(server, databases, per_db_queries);
    writer.Key("databases");
    writer.RawValue(per_db_stats.c_str(), per_db_stats.size(), rapidjson::kArrayType);
  }
  writer.EndObject();

  return buffer.GetString();
}

Result<string> CollectYsqlClusterStatsJson(TabletServerIf* server) {
  auto databases = VERIFY_RESULT(GetDbs(server));
  return BuildStatsJson(
      server, databases, YsqlClusterQueries::kClusterLevel, YsqlClusterQueries::kDbLevel);
}

}  // namespace tserver
}  // namespace yb
