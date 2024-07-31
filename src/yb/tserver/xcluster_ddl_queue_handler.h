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

#include <rapidjson/document.h>
#include "yb/tserver/xcluster_consumer_if.h"

namespace yb {
struct YsqlFullTableName;
namespace tserver {

struct XClusterOutputClientResponse;

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
      const NamespaceId& namespace_id, TserverXClusterContextIf& xcluster_context,
      ConnectToPostgresFunc connect_to_pg_func);
  virtual ~XClusterDDLQueueHandler();

  Status ProcessDDLQueueTable(const XClusterOutputClientResponse& response);

 private:
  friend class XClusterDDLQueueHandlerMocked;

  Status RunAndLogQuery(const std::string& query);

  struct DDLQueryInfo {
    std::string query;
    int64 start_time;
    int64 query_id;
    int version;
    std::string command_tag;
    std::string schema = "";
    std::string user = "";

    std::string ToString() const {
      return YB_STRUCT_TO_STRING(query, start_time, query_id, version, command_tag, schema, user);
    }
  };

  Result<DDLQueryInfo> GetDDLQueryInfo(rapidjson::Document& doc, int64 start_time, int64 query_id);

  virtual Status ProcessDDLQuery(const DDLQueryInfo& query_info);

  virtual Result<bool> CheckIfAlreadyProcessed(const DDLQueryInfo& query_info);

  Status ProcessManualExecutionQuery(const DDLQueryInfo& query_info);

  virtual Status InitPGConnection();
  virtual Result<HybridTime> GetXClusterSafeTimeForNamespace();
  virtual Result<std::vector<std::tuple<int64, int64, std::string>>> GetRowsToProcess(
      const HybridTime& apply_safe_time);

  // Sets xcluster_context with the mapping of table name -> source table id.
  Status ProcessNewRelations(
      rapidjson::Document& doc, const std::string& schema,
      std::vector<YsqlFullTableName>& new_relations);

  client::YBClient* local_client_;

  std::unique_ptr<pgwrapper::PGConn> pg_conn_;
  NamespaceName namespace_name_;
  NamespaceId namespace_id_;
  TserverXClusterContextIf& xcluster_context_;
  ConnectToPostgresFunc connect_to_pg_func_;

  // Whether we have applied new rows to the ddl_queue table since the last apply_safe_time update.
  // If false we can skip processing new DDLs.
  // Set to true initially to handle any rows written but not processed from previous pollers.
  bool applied_new_records_ = true;
};
}  // namespace tserver
}  // namespace yb
