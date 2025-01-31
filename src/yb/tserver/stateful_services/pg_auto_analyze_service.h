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

typedef std::function<Result<pgwrapper::PGConn>(const std::string&,
  const std::optional<CoarseTimePoint>&)> ConnectToPostgresFunc;

class PgAutoAnalyzeService : public StatefulRpcServiceBase<PgAutoAnalyzeServiceIf> {
 public:
  explicit PgAutoAnalyzeService(
      const scoped_refptr<MetricEntity>& metric_entity,
      const std::shared_future<client::YBClient*>& client_future,
      ConnectToPostgresFunc connect_to_pg_func);

 private:
  using NamespaceTablesMap = std::unordered_map<NamespaceId, std::vector<TableId>>;
  using TableMutationsMap = std::unordered_map<TableId, int64_t>;

  void Activate() override;
  void Deactivate() override;
  virtual uint32 PeriodicTaskIntervalMs() const override;
  virtual Result<bool> RunPeriodicTask() override;
  Status FlushMutationsToServiceTable();
  Status TriggerAnalyze();
  Result<TableMutationsMap> ReadTableMutations();
  Status GetTablePGSchemaAndName(const TableMutationsMap& table_id_to_mutations_maps);
  Status FetchUnknownReltuples(
      const TableMutationsMap& table_id_to_mutations_maps,
      std::unordered_set<NamespaceId>& deleted_databases);
  Result<NamespaceTablesMap> DetermineTablesForAnalyze(
      const TableMutationsMap& table_id_to_mutations_maps);
  Result<std::pair<std::vector<TableId>, std::vector<TableId>>>
      DoAnalyzeOnCandidateTables(
          const NamespaceTablesMap& namespace_id_to_analyze_target_tables,
          std::unordered_set<NamespaceId>& deleted_databases);
  Status UpdateTableMutationsAfterAnalyze(
      const std::vector<TableId>& tables,
      const TableMutationsMap& table_id_to_mutations_maps);
  Status CleanUpDeletedTablesFromServiceTable(
      const TableMutationsMap& table_id_to_mutations_maps,
      const std::vector<TableId>& deleted_tables,
      const std::unordered_set<NamespaceId>& deleted_databases);
  Result<pgwrapper::PGConn> EstablishDBConnection(
      const NamespaceId& namespace_id,
      std::unordered_set<NamespaceId>& deleted_databases,
      bool* is_deleted_or_renamed);
  std::string TableNamesForAnalyzeCmd(const std::vector<TableId>& table_ids);

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
  // Each postgres database has its own pg_class table, so we use map instead of single value.
  TableMutationsMap pg_class_id_mutations_;
};

}  // namespace stateful_service
}  // namespace yb
