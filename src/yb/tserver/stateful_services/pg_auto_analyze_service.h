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

#include <map>

#include "yb/tserver/pg_mutation_counter.h"
#include "yb/tserver/stateful_services/pg_auto_analyze_service.service.h"
#include "yb/tserver/stateful_services/stateful_service_base.h"

namespace yb {

namespace pgwrapper {
class PGConn;
}

namespace stateful_service {

typedef std::function<Result<pgwrapper::PGConn>(const std::string&)>
  ConnectToPostgresFunc;

class PgAutoAnalyzeService : public StatefulRpcServiceBase<PgAutoAnalyzeServiceIf> {
 public:
  explicit PgAutoAnalyzeService(
      const scoped_refptr<MetricEntity>& metric_entity,
      const std::shared_future<client::YBClient*>& client_future,
      ConnectToPostgresFunc connect_to_pg_func);

 private:
  void Activate() override;
  void Deactivate() override;
  virtual uint32 PeriodicTaskIntervalMs() const override;
  virtual Result<bool> RunPeriodicTask() override;
  Status FlushMutationsToServiceTable();
  Status TriggerAnalyze();
  Result<std::unordered_map<TableId, int64_t>> ReadTableMutations();
  Status GetTablePGSchemaAndName(
    std::unordered_map<TableId, int64_t>& table_id_to_mutations_maps);
  Status FetchUnknownReltuples(
      std::unordered_map<TableId, int64_t>& table_id_to_mutations_maps);
  Result<std::unordered_map<NamespaceName, std::set<TableId>>> DetermineTablesForAnalyze(
      std::unordered_map<TableId, int64_t>& table_id_to_mutations_maps);
  Result<std::vector<TableId>> DoAnalyzeOnCandidateTables(
      std::unordered_map<NamespaceName, std::set<TableId>>& dbname_to_analyze_target_tables);
  Status UpdateTableMutationsAfterAnalyze(
      std::vector<TableId>& tables,
      std::unordered_map<TableId, int64_t>& table_id_to_mutations_maps);

  STATEFUL_SERVICE_IMPL_METHODS(
      IncreaseMutationCounters);

  tserver::PgMutationCounter pg_cluster_level_mutation_counter_;

  const std::shared_future<client::YBClient*>& client_future_;

  ConnectToPostgresFunc connect_to_pg_func_;

  // In-memory mapping from table id to its number of tuples.
  // Used to calculate analyze threshold for each table.
  std::unordered_map<TableId, float> table_tuple_count_;

  // In-memory mapping for PG tables' name lookup.
  std::unordered_map<TableId, client::YBTableName> table_id_to_name_;

  // In-memory mapping for namespace id to namespace name lookup.
  std::unordered_map<NamespaceId, NamespaceName> namespace_id_to_name_;

  // Track if we need to refresh table_id_to_name_ and namespace_id_to_name_
  // in case of table and database rename.
  bool refresh_name_cache_;
};

}  // namespace stateful_service
}  // namespace yb
