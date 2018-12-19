// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/master/master-path-handlers.h"

#include <algorithm>
#include <functional>
#include <map>
#include <iomanip>
#include <unordered_set>

#include "yb/common/partition.h"
#include "yb/common/schema.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/master/master.h"
#include "yb/master/master.pb.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"
#include "yb/server/webui_util.h"
#include "yb/util/curl_util.h"
#include "yb/util/string_case.h"
#include "yb/util/url-coding.h"
#include "yb/util/version_info.h"
#include "yb/util/version_info.pb.h"

namespace yb {

using consensus::RaftPeerPB;
using std::vector;
using std::map;
using std::string;
using std::stringstream;
using std::unique_ptr;
using strings::Substitute;

using namespace std::placeholders;

namespace master {

constexpr int64_t kBytesPerGB = 1000000000;
constexpr int64_t kBytesPerMB = 1000000;
constexpr int64_t kBytesPerKB = 1000;

MasterPathHandlers::~MasterPathHandlers() {
}

string MasterPathHandlers::BytesToHumanReadable(uint64_t bytes) {
  std::ostringstream op_stream;
  op_stream <<std::setprecision(output_precision_);
  if (bytes >= kBytesPerGB) {
    op_stream << static_cast<double> (bytes)/kBytesPerGB << " GB";
  } else if (bytes >= kBytesPerMB) {
    op_stream << static_cast<double> (bytes)/kBytesPerMB << " MB";
  } else if (bytes >= kBytesPerKB) {
    op_stream << static_cast<double> (bytes)/kBytesPerKB << " KB";
  } else {
    op_stream << bytes << " B";
  }
  return op_stream.str();
}

void MasterPathHandlers::CallIfLeaderOrPrintRedirect(
    const Webserver::WebRequest& req, stringstream* output,
    const Webserver::PathHandlerCallback& callback) {
  string redirect;
  // Lock the CatalogManager in a self-contained block, to prevent double-locking on callbacks.
  {
    CatalogManager::ScopedLeaderSharedLock l(master_->catalog_manager());

    // If we are the master leader, handle the request.
    if (l.first_failed_status().ok()) {
      callback(req, output);
      return;
    }

    // List all the masters.
    vector<ServerEntryPB> masters;
    Status s = master_->ListMasters(&masters);
    if (!s.ok()) {
      s = s.CloneAndPrepend("Unable to list Masters");
      LOG(WARNING) << s.ToString();
      *output << "<h2>" << s.ToString() << "</h2>\n";
      return;
    }

    // Prepare the query for the master leader.

    for (const ServerEntryPB &master : masters) {
      if (master.has_error()) {
        *output << "<h2>" << "Error listing all masters." << "</h2>\n";
        return;
      }

      if (master.role() == consensus::RaftPeerPB::LEADER) {
        // URI already starts with a /, so none is needed between $1 and $2.
        redirect = Substitute("http://$0:$1$2$3",
                              master.registration().http_addresses(0).host(),
                              master.registration().http_addresses(0).port(),
                              req.redirect_uri,
                              req.query_string.empty() ? "?raw" : "?" + req.query_string + "&raw");
        break;
      }
    }
  }

  // Error out if we do not have a redirect URL to the current master leader.
  if (redirect.empty()) {
    *output << "<h2>" << "Error querying master leader." << "</h2>\n";
    return;
  }

  // Make a curl call to the current master leader and return that payload as the result of the
  // web request.
  EasyCurl curl;
  faststring buf;
  Status s = curl.FetchURL(redirect, &buf);
  if (!s.ok()) {
    *output << "<h2>" << "Error querying url " << redirect << "</h2>\n";
    return;
  }
  *output << buf.ToString();
}

inline void MasterPathHandlers::TServerTable(std::stringstream* output) {
  *output << "<table class='table table-striped'>\n";
  *output << "    <tr>\n"
          << "      <th>Server</th>\n"
          << "      <th>Time since </br>heartbeat</th>\n"
          << "      <th>Status & Uptime</th>\n"
          << "      <th>Load (Num Tablets)</th>\n"
          << "      <th>Leader Count</br>(Num Tablets)</th>\n"
          << "      <th>RAM Used</th>\n"
          << "      <th>SST Files Size</th>\n"
          << "      <th>Uncompressed SST </br>Files Size</th>\n"
          << "      <th>Read ops/sec</th>\n"
          << "      <th>Write ops/sec</th>\n"
          << "      <th>Cloud</th>\n"
          << "      <th>Region</th>\n"
          << "      <th>Zone</th>\n"
          << "      <th>UUID</th>\n"
          << "    </tr>\n";
}

namespace {

constexpr int kHoursPerDay = 24;
constexpr int kSecondsPerMinute = 60;
constexpr int kMinutesPerHour = 60;
constexpr int kSecondsPerHour = kSecondsPerMinute * kMinutesPerHour;
constexpr int kMinutesPerDay = kMinutesPerHour * kHoursPerDay;
constexpr int kSecondsPerDay = kSecondsPerHour * kHoursPerDay;

string UptimeString(uint64_t seconds) {
  int days = seconds / kSecondsPerDay;
  int hours = (seconds / kSecondsPerHour) - (days * kHoursPerDay);
  int mins = (seconds / kSecondsPerMinute) - (days * kMinutesPerDay) - (hours * kMinutesPerHour);

  std::ostringstream uptime_string_stream;
  uptime_string_stream << " ";
  if (days > 0) {
    uptime_string_stream << days << "days, ";
  }
  uptime_string_stream << hours << ":" << std::setw(2) << std::setfill('0') << mins <<
      ":" << std::setw(2) << std::setfill('0') << (seconds % 60);

  return uptime_string_stream.str();
}

} // anonymous namespace

void MasterPathHandlers::TServerDisplay(const std::string& current_uuid,
                                        std::vector<std::shared_ptr<TSDescriptor>>* descs,
                                        std::stringstream* output) {
  for (auto desc : *descs) {
    if (desc->placement_uuid() == current_uuid) {
      const string time_since_hb = StringPrintf("%.1fs", desc->TimeSinceHeartbeat().ToSeconds());
      TSRegistrationPB reg;
      desc->GetRegistration(&reg);
      string host_port = Substitute("$0:$1",
                                    reg.common().http_addresses(0).host(),
                                    reg.common().http_addresses(0).port());
      *output << "  <tr>\n";
      *output << "    <td>" << RegistrationToHtml(reg.common(), host_port) << "</td>";
      *output << "    <td>" << time_since_hb << "</td>";
      bool is_dead = false;
      if (master_->ts_manager()->IsTSLive(desc)) {
        *output << "    <td style=\"color:Green\">" << kTserverAlive << ":" <<
                UptimeString(desc->uptime_seconds()) << "</td>";
      } else {
        is_dead = true;
        *output << "    <td style=\"color:Red\">" << kTserverDead << "</td>";
      }

      *output << "    <td>" << (is_dead ? 0 : desc->num_live_replicas()) << "</td>";
      *output << "    <td>" << (is_dead ? 0 : desc->leader_count()) << "</td>";
      *output << "    <td>" << BytesToHumanReadable
                               (desc->total_memory_usage()) << "</td>";
      *output << "    <td>" << BytesToHumanReadable
                               (desc->total_sst_file_size()) << "</td>";
      *output << "    <td>" << BytesToHumanReadable
                             (desc->uncompressed_sst_file_size()) << "</td>";
      *output << "    <td>" << desc->read_ops_per_sec() << "</td>";
      *output << "    <td>" << desc->write_ops_per_sec() << "</td>";
      *output << "    <td>" << reg.common().cloud_info().placement_cloud() << "</td>";
      *output << "    <td>" << reg.common().cloud_info().placement_region() << "</td>";
      *output << "    <td>" << reg.common().cloud_info().placement_zone() << "</td>";
      *output << "    <td>" << desc->permanent_uuid() << "</td>";
      *output << "  </tr>\n";
    }
  }
  *output << "</table>\n";
}

void MasterPathHandlers::HandleTabletServers(const Webserver::WebRequest& req,
                                             stringstream* output) {
  master_->catalog_manager()->AssertLeaderLockAcquiredForReading();

  SysClusterConfigEntryPB config;
  Status s = master_->catalog_manager()->GetClusterConfig(&config);
  if (!s.ok()) {
    *output << "<div class=\"alert alert-warning\">" << s.ToString() << "</div>";
    return;
  }

  auto live_id = config.replication_info().live_replicas().placement_uuid();

  vector<std::shared_ptr<TSDescriptor> > descs;
  const auto& ts_manager = master_->ts_manager();
  ts_manager->GetAllDescriptors(&descs);

  unordered_set<string> read_replica_uuids;
  for (auto desc : descs) {
    if (!read_replica_uuids.count(desc->placement_uuid()) && desc->placement_uuid() != live_id) {
      read_replica_uuids.insert(desc->placement_uuid());
    }
  }

  *output << std::setprecision(output_precision_);
  *output << "<h2>Tablet Servers</h2>\n";


  *output << "<h3 style=\"color:" << kYBDarkBlue << "\">Primary Cluster UUID: "
          << (live_id.empty() ? kNoPlacementUUID : live_id) << "</h3>\n";

  TServerTable(output);
  TServerDisplay(live_id, &descs, output);

  for (const auto& read_replica_uuid : read_replica_uuids) {
    *output << "<h3 style=\"color:" << kYBDarkBlue << "\">Read Replica UUID: "
            << (read_replica_uuid.empty() ? kNoPlacementUUID : read_replica_uuid) << "</h3>\n";
    TServerTable(output);
    TServerDisplay(read_replica_uuid, &descs, output);
  }
}

void MasterPathHandlers::HandleCatalogManager(const Webserver::WebRequest& req,
                                              stringstream* output,
                                              bool skip_system_tables) {
  master_->catalog_manager()->AssertLeaderLockAcquiredForReading();

  vector<scoped_refptr<TableInfo> > tables;
  master_->catalog_manager()->GetAllTables(&tables);

  typedef map<string, string> StringMap;

  // The first stores user tables, the second index tables, and the third system tables.
  std::unique_ptr<StringMap> ordered_tables[kNumTypes];
  for (int i = 0; i < kNumTypes; ++i) {
    ordered_tables[i] = std::make_unique<StringMap>();
  }

  TableType table_cat;
  for (const scoped_refptr<TableInfo>& table : tables) {
    auto l = table->LockForRead();
    if (!l->data().is_running()) {
      continue;
    }

    table_cat = kUserTable;
    // Determine the table category.
    if (IsSystemTable(*table)) {
      // Skip system tables if we should.
      if (skip_system_tables) {
        continue;
      }
      table_cat = kSystemTable;
    } else if (!table->indexed_table_id().empty()) {
      table_cat = kIndexTable;
    }

    const TableName long_table_name = TableLongName(
        master_->catalog_manager()->GetNamespaceName(table->namespace_id()), l->data().name());
    string keyspace = master_->catalog_manager()->GetNamespaceName(table->namespace_id());
    string state = SysTablesEntryPB_State_Name(l->data().pb.state());
    Capitalize(&state);
    (*ordered_tables[table_cat])[long_table_name] = Substitute(
        "<tr><td>$0</td><td><a href=\"/table?id=$4\">$1</a>"
            "</td><td>$2</td><td>$3</td><td>$4</td></tr>\n",
        EscapeForHtmlToString(keyspace),
        EscapeForHtmlToString(l->data().name()),
        state,
        EscapeForHtmlToString(l->data().pb.state_msg()),
        EscapeForHtmlToString(table->id()));
  }

  for (int i = 0; i < kNumTypes; ++i) {
    if (skip_system_tables && table_type_[i] == "System") {
      continue;
    }

    (*output) << "<div class='panel panel-default'>\n"
              << "<div class='panel-heading'><h2 class='panel-title'>" << table_type_[i]
              << " Tables</h2></div>\n";
    (*output) << "<div class='panel-body table-responsive'>";

    if (ordered_tables[i]->empty()) {
      (*output) << "There are no " << static_cast<char>(tolower(table_type_[i][0]))
                << table_type_[i].substr(1) << " type tables.\n";
    } else {
      *output << "<table class='table table-striped' style='table-layout: fixed;'>\n";
      *output << "  <tr><th style='width: 2*;'>Keyspace</th>\n"
              << "      <th style='width: 3*;'>Table Name</th>\n"
              << "      <th style='width: 1*;'>State</th>\n"
              << "      <th style='width: 2*'>Message</th>\n"
              << "      <th style='width: 4*'>UUID</th></tr>\n";
      for (const StringMap::value_type &table : *(ordered_tables[i])) {
        *output << table.second;
      }
      (*output) << "</table>\n";
    }
    (*output) << "</div> <!-- panel-body -->\n";
    (*output) << "</div> <!-- panel -->\n";
  }
}

namespace {

bool CompareByRole(const TabletReplica& a, const TabletReplica& b) {
  return a.role < b.role;
}

} // anonymous namespace


void MasterPathHandlers::HandleTablePage(const Webserver::WebRequest& req,
                                         stringstream *output) {
  master_->catalog_manager()->AssertLeaderLockAcquiredForReading();

  // True if table_id, false if (keyspace, table).
  bool has_id = false;
  if (ContainsKey(req.parsed_args, "id")) {
    has_id = true;
  } else if (ContainsKey(req.parsed_args, "keyspace_name") &&
             ContainsKey(req.parsed_args, "table_name")) {
    has_id = false;
  } else {
    *output << " Missing 'id' argument or 'keyspace_name, table_name' argument pair.";
    *output << " Arguments must either contain the table id or the "
               " (keyspace_name, table_name) pair.";
    return;
  }

  scoped_refptr<TableInfo> table;

  if (has_id) {
    string table_id;
    FindCopy(req.parsed_args, "id", &table_id);
    table = master_->catalog_manager()->GetTableInfo(table_id);
  } else {
    string keyspace, table_name;
    FindCopy(req.parsed_args, "table_name", &table_name);
    FindCopy(req.parsed_args, "keyspace_name", &keyspace);
    table = master_->catalog_manager()
        ->GetTableInfoFromNamespaceNameAndTableName(keyspace, table_name);
  }

  if (table == nullptr) {
    *output << "Table not found!";
    return;
  }

  Schema schema;
  PartitionSchema partition_schema;
  NamespaceName keyspace_name;
  TableName table_name;
  vector<scoped_refptr<TabletInfo> > tablets;
  {
    auto l = table->LockForRead();
    keyspace_name = master_->catalog_manager()->GetNamespaceName(table->namespace_id());
    table_name = l->data().name();
    *output << "<h1>Table: " << EscapeForHtmlToString(TableLongName(keyspace_name, table_name))
            << " ("<< table->id() <<") </h1>\n";

    *output << "<table class='table table-striped'>\n";
    *output << "  <tr><td>Version:</td><td>" << l->data().pb.version() << "</td></tr>\n";

    *output << "  <tr><td>Type:</td><td>" << TableType_Name(l->data().pb.table_type())
            << "</td></tr>\n";

    string state = SysTablesEntryPB_State_Name(l->data().pb.state());
    Capitalize(&state);
    *output << "  <tr><td>State:</td><td>"
            << state
            << EscapeForHtmlToString(l->data().pb.state_msg())
            << "</td></tr>\n";
    *output << "</table>\n";

    SchemaFromPB(l->data().pb.schema(), &schema);
    Status s = PartitionSchema::FromPB(l->data().pb.partition_schema(), schema, &partition_schema);
    if (!s.ok()) {
      *output << "Unable to decode partition schema: " << s.ToString();
      return;
    }
    table->GetAllTablets(&tablets);
  }

  HtmlOutputSchemaTable(schema, output);

  *output << "<table class='table table-striped'>\n";
  *output << "  <tr><th>Tablet ID</th><th>Partition</th><th>State</th>"
      "<th>Message</th><th>RaftConfig</th></tr>\n";
  for (const scoped_refptr<TabletInfo>& tablet : tablets) {
    TabletInfo::ReplicaMap locations;
    tablet->GetReplicaLocations(&locations);
    vector<TabletReplica> sorted_locations;
    AppendValuesFromMap(locations, &sorted_locations);
    std::sort(sorted_locations.begin(), sorted_locations.end(), &CompareByRole);

    auto l = tablet->LockForRead();

    Partition partition;
    Partition::FromPB(l->data().pb.partition(), &partition);

    string state = SysTabletsEntryPB_State_Name(l->data().pb.state());
    Capitalize(&state);
    *output << Substitute(
        "<tr><th>$0</th><td>$1</td><td>$2</td><td>$3</td><td>$4</td></tr>\n",
        tablet->tablet_id(),
        EscapeForHtmlToString(partition_schema.PartitionDebugString(partition, schema)),
        state,
        EscapeForHtmlToString(l->data().pb.state_msg()),
        RaftConfigToHtml(sorted_locations, tablet->tablet_id()));
  }
  *output << "</table>\n";

  HtmlOutputTasks(table->GetTasks(), output);
}

bool MasterPathHandlers::IsSystemTable(const TableInfo& table) {
  return master_->catalog_manager()->IsSystemTable(table) || table.IsRedisTable();
}

void MasterPathHandlers::RootHandler(const Webserver::WebRequest& req,
                                     stringstream* output) {

  // First check if we are the master leader. If not, make a curl call to the master leader and
  // return that as the UI payload.
  vector<ServerEntryPB> masters;
  CatalogManager::ScopedLeaderSharedLock l(master_->catalog_manager());
  if (!l.first_failed_status().ok()) {
    do {
      // List all the masters.
      Status s = master_->ListMasters(&masters);
      if (!s.ok()) {
        s = s.CloneAndPrepend("Unable to list Masters");
        LOG(WARNING) << s.ToString();
        *output << "<h2>" << s.ToString() << "</h2>\n";
        return;
      }

      // Find the URL of the current master leader.
      string redirect;
      for (const ServerEntryPB& master : masters) {
        if (master.has_error()) {
          // This will leave redirect empty and thus fail accordingly.
          break;
        }

        if (master.role() == consensus::RaftPeerPB::LEADER) {
          // URI already starts with a /, so none is needed between $1 and $2.
          redirect = Substitute("http://$0:$1$2$3",
                                master.registration().http_addresses(0).host(),
                                master.registration().http_addresses(0).port(),
                                req.redirect_uri,
                                req.query_string.empty() ? "?raw" :
                                                           "?" + req.query_string + "&raw");
        }
      }
      // Fail if we were not able to find the current master leader.
      if (redirect.empty()) {
        break;
      }
      // Make a curl call to the current master leader and return that payload as the result of the
      // web request.
      EasyCurl curl;
      faststring buf;
      s = curl.FetchURL(redirect, &buf);
      if (s.ok()) {
        *output << buf.ToString();
        return;
      }
    } while (0);

    *output << "Cannot get Leader information to help you redirect...\n";
    return;
  }

  SysClusterConfigEntryPB config;
  Status s = master_->catalog_manager()->GetClusterConfig(&config);
  if (!s.ok()) {
    *output << "<div class=\"alert alert-warning\">" << s.ToString() << "</div>";
    return;
  }

  // Get all the tables.
  vector<scoped_refptr<TableInfo> > tables;
  master_->catalog_manager()->GetAllTables(&tables, true /* includeOnlyRunningTables */);

  // Get the list of user tables.
  vector<scoped_refptr<TableInfo> > user_tables;
  for (scoped_refptr<TableInfo> table : tables) {
    if (!IsSystemTable(*table)) {
      user_tables.push_back(table);
    }
  }
  // Get the version info.
  VersionInfoPB version_info;
  VersionInfo::GetVersionInfoPB(&version_info);

  // Display the overview information.
  (*output) << "<h1>YugaByte DB</h1>\n";

  (*output) << "<div class='row dashboard-content'>\n";

  (*output) << "<div class='col-xs-12 col-md-8 col-lg-6'>\n";
  (*output) << "<div class='panel panel-default'>\n"
            << "<div class='panel-heading'><h2 class='panel-title'> Overview</h2></div>\n";
  (*output) << "<div class='panel-body table-responsive'>";
  (*output) << "<table class='table'>\n";

  // Universe UUID.
  (*output) << "  <tr>";
  (*output) << Substitute(" <td>$0<span class='yb-overview'>$1</span></td>",
                          "<i class='fa fa-database yb-dashboard-icon' aria-hidden='true'></i>",
                          "Universe UUID ");
  (*output) << Substitute(" <td>$0</td>",
                          config.cluster_uuid());
  (*output) << "  </tr>\n";

  // Replication factor.
  (*output) << "  <tr>";
  (*output) << Substitute(" <td>$0<span class='yb-overview'>$1</span></td>",
                          "<i class='fa fa-files-o yb-dashboard-icon' aria-hidden='true'></i>",
                          "Replication Factor ");
  int num_replicas = 0;
  s = master_->catalog_manager()->GetReplicationFactor(&num_replicas);
  if (!s.ok()) {
    s = s.CloneAndPrepend("Unable to determine Replication factor.");
    LOG(WARNING) << s.ToString();
    *output << "<h1>" << s.ToString() << "</h1>\n";
  }
  (*output) << Substitute(" <td>$0 <a href='$1' class='btn btn-default pull-right'>$2</a></td>",
                          num_replicas,
                          "/cluster-config",
                          "See full config &raquo;");
  (*output) << "  </tr>\n";

  // Tserver count.
  (*output) << "  <tr>";
  (*output) << Substitute(" <td>$0<span class='yb-overview'>$1</span></td>",
                          "<i class='fa fa-server yb-dashboard-icon' aria-hidden='true'></i>",
                          "Num Nodes (TServers) ");
  (*output) << Substitute(" <td>$0 <a href='$1' class='btn btn-default pull-right'>$2</a></td>",
                          master_->ts_manager()->GetCount(),
                          "/tablet-servers",
                          "See all nodes &raquo;");
  (*output) << "  </tr>\n";

  // Num user tables.
  (*output) << "  <tr>";
  (*output) << Substitute(" <tr><td>$0<span class='yb-overview'>$1</span></td>",
                          "<i class='fa fa-table yb-dashboard-icon' aria-hidden='true'></i>",
                          "Num User Tables ");
  (*output) << Substitute(" <td>$0 <a href='$1' class='btn btn-default pull-right'>$2</a></td>",
                          user_tables.size(),
                          "/tables",
                          "See all tables &raquo;");
  (*output) << "  </tr>\n";

  // Build version and type.
  (*output) << Substitute("  <tr><td>$0<span class='yb-overview'>$1</span></td><td>$2</td></tr>\n",
                          "<i class='fa fa-code-fork yb-dashboard-icon' aria-hidden='true'></i>",
                          "YugaByte Version ", version_info.version_number());
  (*output) << Substitute("  <tr><td>$0<span class='yb-overview'>$1</span></td><td>$2</td></tr>\n",
                          "<i class='fa fa-terminal yb-dashboard-icon' aria-hidden='true'></i>",
                          "Build Type ", version_info.build_type());
  (*output) << "</table>";
  (*output) << "</div> <!-- panel-body -->\n";
  (*output) << "</div> <!-- panel -->\n";
  (*output) << "</div> <!-- col-xs-12 col-md-8 col-lg-6 -->\n";

  // Display the master info.
  (*output) << "<div class='col-xs-12 col-md-8 col-lg-6'>\n";
  HandleMasters(req, output);
  (*output) << "</div> <!-- col-xs-12 col-md-8 col-lg-6 -->\n";

  // Display the user tables if any.
  (*output) << "<div class='col-md-12 col-lg-12'>\n";
  HandleCatalogManager(req, output, true /* skip_system_tables */);
  (*output) << "</div> <!-- col-md-12 col-lg-12 -->\n";
}

void MasterPathHandlers::HandleMasters(const Webserver::WebRequest& req,
                                       stringstream* output) {
  vector<ServerEntryPB> masters;
  Status s = master_->ListMasters(&masters);
  if (!s.ok()) {
    s = s.CloneAndPrepend("Unable to list Masters");
    LOG(WARNING) << s.ToString();
    *output << "<h1>" << s.ToString() << "</h1>\n";
    return;
  }
  (*output) << "<div class='panel panel-default'>\n"
            << "<div class='panel-heading'><h2 class='panel-title'>Masters</h2></div>\n";
  (*output) << "<div class='panel-body table-responsive'>";
  (*output) << "<table class='table'>\n";
  (*output) << "  <tr>\n"
            << "    <th>Server</th>\n"
            << "    <th>RAFT Role</th>"
            << "    <th>Details</th>\n"
            << "  </tr>\n";

  for (const ServerEntryPB& master : masters) {
    if (master.has_error()) {
      string error = StatusFromPB(master.error()).ToString();
      *output << "  <tr>\n";
      const string kErrStart = "peer ([";
      const string kErrEnd = "])";
      size_t start_pos = error.find(kErrStart);
      size_t end_pos = error.find(kErrEnd);
      if (start_pos != string::npos && end_pos != string::npos && (start_pos < end_pos)) {
        start_pos = start_pos + kErrStart.length();
        string host_port = error.substr(start_pos, end_pos - start_pos);
        *output << "<td><font color='red'>" << EscapeForHtmlToString(host_port)
                << "</font></td>\n";
        *output << "<td><font color='red'>" << RaftPeerPB_Role_Name(RaftPeerPB::UNKNOWN_ROLE)
                << "</font></td>\n";
      }
      *output << Substitute("    <td colspan=2><font color='red'><b>ERROR: $0</b></font></td>\n",
                              EscapeForHtmlToString(error));
      *output << "  </tr>\n";
      continue;
    }
    auto reg = master.registration();
    string host_port = Substitute("$0:$1",
                                  reg.http_addresses(0).host(), reg.http_addresses(0).port());
    string reg_text = RegistrationToHtml(reg, host_port);
    if (master.instance_id().permanent_uuid() == master_->instance_pb().permanent_uuid()) {
      reg_text = Substitute("<b>$0</b>", reg_text);
    }
    string raft_role = master.has_role() ? RaftPeerPB_Role_Name(master.role()) : "N/A";
    string cloud = reg.cloud_info().placement_cloud();
    string region = reg.cloud_info().placement_region();
    string zone = reg.cloud_info().placement_zone();

    *output << "  <tr>\n"
            << "    <td>" << reg_text << "</td>\n"
            << "    <td>" << raft_role << "</td>\n"
            << "    <td><div><span class='yb-overview'>CLOUD: </span>" << cloud << "</div>\n"
            << "        <div><span class='yb-overview'>REGION: </span>" << region << "</div>\n"
            << "        <div><span class='yb-overview'>ZONE: </span>" << zone << "</div>\n"
            << "        <div><span class='yb-overview'>UUID: </span>"
            << master.instance_id().permanent_uuid()
            << "</div></td>\n"
            << "  </tr>\n";
  }

  (*output) << "</table>";
  (*output) << "</div> <!-- panel-body -->\n";
  (*output) << "</div> <!-- panel -->\n";
}

namespace {

// Visitor for the catalog table which dumps tables and tablets in a JSON format. This
// dump is interpreted by the CM agent in order to track time series entities in the SMON
// database.
//
// This implementation relies on scanning the catalog table directly instead of using the
// catalog manager APIs. This allows it to work even on a non-leader master, and avoids
// any requirement for locking. For the purposes of metrics entity gathering, it's OK to
// serve a slightly stale snapshot.
//
// It is tempting to directly dump the metadata protobufs using JsonWriter::Protobuf(...),
// but then we would be tying ourselves to textual compatibility of the PB field names in
// our catalog table. Instead, the implementation specifically dumps the fields that we
// care about.
//
// This should be considered a "stable" protocol -- do not rename, remove, or restructure
// without consulting with the CM team.
class JsonDumperBase {
 public:
  explicit JsonDumperBase(JsonWriter* jw) : jw_(jw) {}

  virtual ~JsonDumperBase() {}

  virtual std::string name() const = 0;

 protected:
  JsonWriter* jw_;
};

class JsonKeyspaceDumper : public Visitor<PersistentNamespaceInfo>, public JsonDumperBase {
 public:
  explicit JsonKeyspaceDumper(JsonWriter* jw) : JsonDumperBase(jw) {}

  std::string name() const override { return "keyspaces"; }

  virtual Status Visit(const std::string& keyspace_id,
                       const SysNamespaceEntryPB& metadata) override {
    jw_->StartObject();
    jw_->String("keyspace_id");
    jw_->String(keyspace_id);

    jw_->String("keyspace_name");
    jw_->String(metadata.name());

    jw_->EndObject();
    return Status::OK();
  }
};

class JsonTableDumper : public Visitor<PersistentTableInfo>, public JsonDumperBase {
 public:
  explicit JsonTableDumper(JsonWriter* jw) : JsonDumperBase(jw) {}

  std::string name() const override { return "tables"; }

  Status Visit(const std::string& table_id, const SysTablesEntryPB& metadata) override {
    if (metadata.state() != SysTablesEntryPB::RUNNING) {
      return Status::OK();
    }

    jw_->StartObject();
    jw_->String("table_id");
    jw_->String(table_id);

    jw_->String("keyspace_id");
    jw_->String(metadata.namespace_id());

    jw_->String("table_name");
    jw_->String(metadata.name());

    jw_->String("state");
    jw_->String(SysTablesEntryPB::State_Name(metadata.state()));

    jw_->EndObject();
    return Status::OK();
  }
};

class JsonTabletDumper : public Visitor<PersistentTabletInfo>, public JsonDumperBase {
 public:
  explicit JsonTabletDumper(JsonWriter* jw) : JsonDumperBase(jw) {}

  std::string name() const override { return "tablets"; }

  Status Visit(const std::string& tablet_id, const SysTabletsEntryPB& metadata) override {
    const std::string& table_id = metadata.table_id();
    if (metadata.state() != SysTabletsEntryPB::RUNNING) {
      return Status::OK();
    }

    jw_->StartObject();
    jw_->String("table_id");
    jw_->String(table_id);

    jw_->String("tablet_id");
    jw_->String(tablet_id);

    jw_->String("state");
    jw_->String(SysTabletsEntryPB::State_Name(metadata.state()));

    // Dump replica UUIDs
    if (metadata.has_committed_consensus_state()) {
      const consensus::ConsensusStatePB& cs = metadata.committed_consensus_state();
      jw_->String("replicas");
      jw_->StartArray();
      for (const RaftPeerPB& peer : cs.config().peers()) {
        jw_->StartObject();
        jw_->String("type");
        jw_->String(RaftPeerPB::MemberType_Name(peer.member_type()));

        jw_->String("server_uuid");
        jw_->String(peer.permanent_uuid());

        jw_->String("addr");
        const auto& host_port = peer.last_known_private_addr()[0];
        jw_->String(Format("$0:$1", host_port.host(), host_port.port()));

        jw_->EndObject();
      }
      jw_->EndArray();

      if (cs.has_leader_uuid()) {
        jw_->String("leader");
        jw_->String(cs.leader_uuid());
      }
    }

    jw_->EndObject();
    return Status::OK();
  }
};

template <class Dumper>
Status JsonDumpCollection(JsonWriter* jw, Master* master, stringstream* output) {
  unique_ptr<Dumper> json_dumper(new Dumper(jw));
  jw->String(json_dumper->name());
  jw->StartArray();
  const Status s = master->catalog_manager()->sys_catalog()->Visit(json_dumper.get());
  if (s.ok()) {
    // End the array only if there is no error.
    jw->EndArray();
  } else {
    // Print just an error message.
    output->str("");
    JsonWriter jw_err(output, JsonWriter::COMPACT);
    jw_err.StartObject();
    jw_err.String("error");
    jw_err.String(s.ToString());
    jw_err.EndObject();
  }
  return s;
}

} // anonymous namespace

void MasterPathHandlers::HandleDumpEntities(const Webserver::WebRequest& req,
                                            stringstream* output) {
  master_->catalog_manager()->AssertLeaderLockAcquiredForReading();

  JsonWriter jw(output, JsonWriter::COMPACT);
  jw.StartObject();

  if (JsonDumpCollection<JsonKeyspaceDumper>(&jw, master_, output).ok() &&
      JsonDumpCollection<JsonTableDumper>(&jw, master_, output).ok() &&
      JsonDumpCollection<JsonTabletDumper>(&jw, master_, output).ok()) {
    // End the object only if there is no error.
    jw.EndObject();
  }
}

void MasterPathHandlers::HandleGetClusterConfig(
  const Webserver::WebRequest& req, stringstream* output) {
  master_->catalog_manager()->AssertLeaderLockAcquiredForReading();

  *output << "<h1>Current Cluster Config</h1>\n";
  SysClusterConfigEntryPB config;
  Status s = master_->catalog_manager()->GetClusterConfig(&config);
  if (!s.ok()) {
    *output << "<div class=\"alert alert-warning\">" << s.ToString() << "</div>";
    return;
  }

  *output << "<div class=\"alert alert-success\">Successfully got cluster config!</div>"
  << "<pre class=\"prettyprint\">" << config.DebugString() << "</pre>";
}


Status MasterPathHandlers::Register(Webserver* server) {
  bool is_styled = true;
  bool is_on_nav_bar = true;

  // The set of handlers visible on the nav bar.
  server->RegisterPathHandler(
    "/", "Home", std::bind(&MasterPathHandlers::RootHandler, this, _1, _2), is_styled,
    is_on_nav_bar, "fa fa-home");
  Webserver::PathHandlerCallback cb =
      std::bind(&MasterPathHandlers::HandleTabletServers, this, _1, _2);
  server->RegisterPathHandler(
      "/tablet-servers", "Tablet Servers",
      std::bind(&MasterPathHandlers::CallIfLeaderOrPrintRedirect, this, _1, _2, cb), is_styled,
      is_on_nav_bar, "fa fa-server");
  cb = std::bind(&MasterPathHandlers::HandleCatalogManager,
                 this, _1, _2, false /* skip_system_tables */);
  server->RegisterPathHandler(
      "/tables", "Tables",
      std::bind(&MasterPathHandlers::CallIfLeaderOrPrintRedirect, this, _1, _2, cb), is_styled,
      is_on_nav_bar, "fa fa-table");
  cb = std::bind(&MasterPathHandlers::HandleTablePage, this, _1, _2);

  // The set of handlers not visible on the nav bar.
  server->RegisterPathHandler(
      "/table", "", std::bind(&MasterPathHandlers::CallIfLeaderOrPrintRedirect, this, _1, _2, cb),
      is_styled, false);
  server->RegisterPathHandler(
      "/masters", "Masters", std::bind(&MasterPathHandlers::HandleMasters, this, _1, _2), is_styled,
      false);
  cb = std::bind(&MasterPathHandlers::HandleDumpEntities, this, _1, _2);
  server->RegisterPathHandler(
      "/dump-entities", "Dump Entities",
      std::bind(&MasterPathHandlers::CallIfLeaderOrPrintRedirect, this, _1, _2, cb), false, false);
  cb = std::bind(&MasterPathHandlers::HandleGetClusterConfig, this, _1, _2);
  server->RegisterPathHandler(
      "/cluster-config", "Cluster Config",
      std::bind(&MasterPathHandlers::CallIfLeaderOrPrintRedirect, this, _1, _2, cb), is_styled,
      false);
  return Status::OK();
}

string MasterPathHandlers::RaftConfigToHtml(const std::vector<TabletReplica>& locations,
                                            const std::string& tablet_id) const {
  stringstream html;

  html << "<ul>\n";
  for (const TabletReplica& location : locations) {
    string location_html = TSDescriptorToHtml(*location.ts_desc, tablet_id);
    if (location.role == RaftPeerPB::LEADER) {
      html << Substitute("  <li><b>LEADER: $0</b></li>\n", location_html);
    } else {
      html << Substitute("  <li>$0: $1</li>\n",
                         RaftPeerPB_Role_Name(location.role), location_html);
    }
  }
  html << "</ul>\n";
  return html.str();
}

string MasterPathHandlers::TSDescriptorToHtml(const TSDescriptor& desc,
                                              const std::string& tablet_id) const {
  TSRegistrationPB reg;
  desc.GetRegistration(&reg);

  if (reg.common().http_addresses().size() > 0) {
    return Substitute(
        "<a href=\"http://$0:$1/tablet?id=$2\">$3</a>", reg.common().http_addresses(0).host(),
        reg.common().http_addresses(0).port(), EscapeForHtmlToString(tablet_id),
        EscapeForHtmlToString(reg.common().http_addresses(0).host()));
  } else {
    return EscapeForHtmlToString(desc.permanent_uuid());
  }
}

string MasterPathHandlers::RegistrationToHtml(
    const ServerRegistrationPB& reg, const std::string& link_text) const {
  string link_html = EscapeForHtmlToString(link_text);
  if (reg.http_addresses().size() > 0) {
    link_html = Substitute("<a href=\"http://$0:$1/\">$2</a>",
                           reg.http_addresses(0).host(),
                           reg.http_addresses(0).port(), link_html);
  }
  return link_html;
}

} // namespace master
} // namespace yb
