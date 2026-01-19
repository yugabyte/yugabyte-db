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

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <rapidjson/document.h>

#include "yb/cdc/xcluster_types.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/pg_types.h"
#include "yb/tserver/xcluster_consumer_if.h"

namespace yb {
struct YsqlFullTableName;
namespace tserver {

struct XClusterOutputClientResponse;

using UpdateSafeTimeFunc = std::function<void (HybridTime)>;

struct XClusterDDLQueryInfo {
  int64 ddl_end_time;
  int64 query_id;
  std::string query;
  int version = 0;
  std::string command_tag;
  std::string schema;
  std::string user;
  std::string json_for_oid_assignment;
  bool is_manual_execution = false;

  struct RelationInfo {
    std::string relation_name;
    std::string relation_pgschema_name;
    PgOid relfile_oid;
    ColocationId colocation_id;
    bool is_index;
    std::string ToString() const {
      return YB_STRUCT_TO_STRING(
          relation_name, relation_pgschema_name, relfile_oid, colocation_id, is_index);
    }
  };
  std::vector<RelationInfo> relation_map;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(
        query, ddl_end_time, query_id, version, command_tag, schema, user, json_for_oid_assignment,
        is_manual_execution, relation_map);
  }
};

// Handler for the ddl_queue table, used for xCluster DDL replication.
// This handler is called by XClusterPoller after ApplyChanges has been processed successfully for
// the ddl_queue tablet.
// Since the ddl_queue table needs to be the last poller to update safe time, we first verify that
// all other tablets have caught up to this safe time. If not, then we reschedule just the ddl_queue
// processing section.
// Once all other tablets have caught up to this safe time, then the handler will read from
// ddl_queue and rerun the DDLs accordingly.
class XClusterDDLQueueHandler {
 public:
  XClusterDDLQueueHandler(
      client::YBClient* local_client, const NamespaceName& namespace_name,
      const NamespaceId& source_namespace_id, const NamespaceId& target_namespace_id,
      const std::string& log_prefix, TserverXClusterContextIf& xcluster_context,
      ConnectToPostgresFunc connect_to_pg_func, UpdateSafeTimeFunc update_safe_time_func);
  virtual ~XClusterDDLQueueHandler();

  void Shutdown();

  // This function is called before the poller calls GetChanges. This will detect if we are in the
  // middle of a executing a DDL batch and complete it.
  Status ProcessPendingBatchIfExists();

  // To be called after the poller has finished applying changes to the ddl_queue table. If we have
  // a complete batch (ie we have received an apply_safe_time), then we will run the DDLs in the
  // batch. If not, then we will just persist the commit times of the new DDLs in replicated_ddls.
  Status ProcessGetChangesResponse(const XClusterOutputClientResponse& response);

  Status UpdateSafeTimeForPause();

  // Fetch the current batch persisted in the replicated_ddls table.
  static Result<xcluster::SafeTimeBatch> FetchSafeTimeBatchFromReplicatedDdls(
      pgwrapper::PGConn* pg_conn);
  // Set the appropriate GUCs and prepare the statemnts used by the ddl_queue handler.
  static Status RunDdlQueueHandlerPrepareQueries(pgwrapper::PGConn* pg_conn);

 private:
  friend class XClusterDDLQueueHandlerMocked;

  // Common function called by ProcessPendingBatchIfExists and ProcessGetChangesResponse.
  // Executes DDLs at the commit_times in safe_time_batch_ if we have a valid batch.
  Status ExecuteCommittedDDLs();

  Status RunAndLogQuery(const std::string& query);

  // Run the DDL query with the appropriate flags set.
  virtual Status ProcessDDLQuery(const XClusterDDLQueryInfo& query_info);

  // Used to keep track of the number of times we've failed this DDL.
  virtual Status ProcessFailedDDLQuery(const Status& s, const XClusterDDLQueryInfo& query_info);
  // Returns whether we've already failed this query too many times.
  virtual Status CheckForFailedQuery();

  // Checks replicated_ddls table to see if this DDL has already been processed.
  virtual Result<bool> IsAlreadyProcessed(const XClusterDDLQueryInfo& query_info);

  Status ProcessManualExecutionQuery(const XClusterDDLQueryInfo& query_info);

  virtual Status InitPGConnection();
  virtual Result<HybridTime> GetXClusterSafeTimeForNamespace();

  virtual Result<std::vector<std::tuple<int64, int64, std::string>>> GetRowsToProcess(
      const HybridTime& commit_time);

  // Queries ddl_queue at the given apply_safe_time and returns the DDLs to process.
  Result<std::vector<XClusterDDLQueryInfo>> GetQueriesToProcess(const HybridTime& commit_time);

  // Sets xcluster_context with the mapping of table name -> source table id.
  Status ProcessNewRelations(
      const XClusterDDLQueryInfo& query_info, std::unordered_set<YsqlFullTableName>& new_relations,
      const HybridTime& commit_time);

  // Checks replicated_ddls table for an existing batch.
  // If one exists, then will fill out safe_time_batch_ with the commit times and apply safe time
  // (if available). If none exists, then starts an empty batch.
  Status ReloadSafeTimeBatchFromTableIfRequired();

  // Persists the new commit_times into replicated_ddls. If we have a complete batch, then also
  // updates safe_time_batch_ with the new commit times and apply safe time.
  Status PersistAndUpdateSafeTimeBatch(
      const std::set<HybridTime>& commit_times, const HybridTime& apply_safe_time);
  virtual Status DoPersistAndUpdateSafeTimeBatch(xcluster::SafeTimeBatch new_safe_time_batch);

  Status UpdateSafeTimeBatchAfterProcessing(const HybridTime& last_commit_time_processed);

  Status ResetSafeTimeBatchOnFailure(const Status& s);

  const std::string& LogPrefix() const { return log_prefix_; }

  client::YBClient* local_client_;

  std::unique_ptr<pgwrapper::PGConn> pg_conn_;
  NamespaceName namespace_name_;
  NamespaceId source_namespace_id_;
  NamespaceId target_namespace_id_;
  const std::string log_prefix_;
  TserverXClusterContextIf& xcluster_context_;
  ConnectToPostgresFunc connect_to_pg_func_;
  UpdateSafeTimeFunc update_safe_time_func_;

  struct QueryIdentifier {
    int64 ddl_end_time;
    int64 query_id;

    bool MatchesQueryInfo(const XClusterDDLQueryInfo& query_info) const {
      return ddl_end_time == query_info.ddl_end_time && query_id == query_info.query_id;
    }
  };

  // Keep track of how many times we've repeatedly failed a DDL.
  int num_fails_for_this_ddl_ = 0;
  std::optional<QueryIdentifier> last_failed_query_;
  Status last_failed_status_;

  // Cache of the DDL batch in replicated_ddl table. This only set when we are certain that it is up
  // to date with the persisted state. It is set to nullopt in all other cases and needs to be
  // refreshed from the table.
  // This cache is used to avoid repeated queries to the replicated_ddls table for the list of
  // commit times.
  std::optional<xcluster::SafeTimeBatch> safe_time_batch_;
};
}  // namespace tserver
}  // namespace yb
