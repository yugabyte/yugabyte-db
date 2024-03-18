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

#include "yb/tserver/tserver-path-handlers.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/xrepl_stream_stats.h"

#include "yb/common/path-handler-util.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/log_anchor_registry.h"
#include "yb/consensus/quorum_util.h"

#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/human_readable.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/numbers.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/util/options_parser.h"

#include "yb/server/webui_util.h"

#include "yb/tablet/maintenance_manager.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet.pb.h"
#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/tserver/service_util.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/tserver/xcluster_consumer_if.h"
#include "yb/tserver/xcluster_poller_stats.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/url-coding.h"
#include "yb/util/version_info.h"
#include "yb/util/version_info.pb.h"
#include "yb/util/flags.h"

using yb::consensus::GetConsensusRole;
using yb::consensus::CONSENSUS_CONFIG_COMMITTED;
using yb::consensus::ConsensusStatePB;
using yb::consensus::RaftPeerPB;
using yb::consensus::OperationStatusPB;
using yb::tablet::MaintenanceManagerStatusPB;
using yb::tablet::MaintenanceManagerStatusPB_CompletedOpPB;
using yb::tablet::MaintenanceManagerStatusPB_MaintenanceOpPB;
using yb::tablet::Tablet;
using yb::tablet::TabletDataState;
using yb::tablet::TabletPeer;
using yb::tablet::TabletStatusPB;
using yb::tablet::Operation;

using std::endl;
using std::shared_ptr;
using std::vector;
using std::string;
using strings::Substitute;

using namespace std::placeholders;  // NOLINT(build/namespaces)

DEFINE_UNKNOWN_bool(enable_intentsdb_page, false,
    "Enable displaying the contents of intentsdb page.");

DECLARE_bool(enable_xcluster_stat_collection);

namespace {

// A struct representing some information about a tablet peer.
struct TabletPeerInfo {
  string namespace_name;
  string name;
  bool is_hidden;
  uint64_t num_sst_files;
  yb::tablet::TabletOnDiskSizeInfo disk_size_info;
  bool has_on_disk_size;
  yb::PeerRole raft_role;
};

// An identifier for a table, according to the `/tables` page.
struct TableIdentifier {
  string uuid;
  string state;
};

// A struct representing some information about a table.
struct TableInfo {
  string namespace_name;
  string name;
  bool is_hidden;
  uint64_t num_sst_files;
  yb::tablet::TabletOnDiskSizeInfo disk_size_info;
  bool has_complete_on_disk_size;
  std::map<yb::PeerRole, size_t> raft_role_counts;

  explicit TableInfo(TabletPeerInfo info)
      : namespace_name(info.namespace_name),
        name(info.name),
        is_hidden(info.is_hidden),
        num_sst_files(info.num_sst_files),
        disk_size_info(info.disk_size_info),
        has_complete_on_disk_size(info.has_on_disk_size) {
    raft_role_counts.emplace(info.raft_role, 1);
  }

  // Adds information about a single tablet peer to a table.
  void Aggregate(const TabletPeerInfo& other) {
    auto rc_iter = raft_role_counts.find(other.raft_role);
    if (rc_iter == raft_role_counts.end()) {
      raft_role_counts.emplace(other.raft_role, 1);
    } else {
      ++rc_iter->second;
    }

    num_sst_files += other.num_sst_files;
    disk_size_info += other.disk_size_info;
    has_complete_on_disk_size = has_complete_on_disk_size && other.has_on_disk_size;
  }
};

}  // anonymous namespace

namespace std {

template<>
struct less<TableIdentifier> {
  bool operator() (const TableIdentifier& lhs, const TableIdentifier& rhs) const {
    if (lhs.uuid == rhs.uuid) {
      return lhs.state < rhs.state;
    } else {
      return lhs.uuid < rhs.uuid;
    }
  }
};

}  //  namespace std

namespace yb {
namespace tserver {

namespace {

bool GetTabletID(const Webserver::WebRequest& req, string* id, std::stringstream *out) {
  if (!FindCopy(req.parsed_args, "id", id)) {
    // TODO: webserver should give a way to return a non-200 response code
    (*out) << "Tablet missing 'id' argument";
    return false;
  }
  return true;
}

tablet::TabletPeerPtr GetTabletPeer(TabletServer* tserver, const Webserver::WebRequest& req,
                                    const TabletId& tablet_id, std::stringstream *out) {
  auto result = tserver->tablet_manager()->LookupTablet(tablet_id);
  if (!result) {
    (*out) << "Tablet " << EscapeForHtmlToString(tablet_id) << " not found";
  }
  return result;
}

bool TabletReady(const std::shared_ptr<TabletPeer>& peer, const string& tablet_id,
                         std::stringstream* out) {
  auto state = peer->state();
  if (state == tablet::BOOTSTRAPPING) {
    (*out) << "Tablet " << EscapeForHtmlToString(tablet_id) << " is still bootstrapping";
    return false;
  } else if (state == tablet::NOT_STARTED) {
    (*out) << "Tablet " << EscapeForHtmlToString(tablet_id) << " has not yet started";
    return false;
  }
  return true;
}

// Returns true if the tablet_id was properly specified, the
// tablet is found, and is in a non-bootstrapping state.
tablet::TabletPeerPtr LoadTablet(TabletServer* tserver,
                                 const Webserver::WebRequest& req,
                                 string* tablet_id,
                                 std::stringstream* out) {
  if (!GetTabletID(req, tablet_id, out)) return nullptr;
  auto result = GetTabletPeer(tserver, req, *tablet_id, out);
  if (!result || !TabletReady(result, *tablet_id, out)) {
    return nullptr;
  }
  return result;
}

void HandleTabletPage(
    const std::string& tablet_id, const tablet::TabletPeerPtr& peer,
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  string table_name = peer->tablet_metadata()->table_name();

  *output << "<h1>Tablet " << EscapeForHtmlToString(tablet_id) << "</h1>\n";

  // Output schema in tabular format.
  *output << "<h2>Schema</h2>\n";
  const SchemaPtr schema = peer->tablet_metadata()->schema();
  server::HtmlOutputSchemaTable(*schema, output);

  *output << "<h2>Other Tablet Info Pages</h2>" << endl;

  // List of links to various tablet-specific info pages
  *output << "<ul>";

  std::initializer_list<std::array<const char*, 2>> entries = {
      {"tablet-consensus-status", "Consensus Status"},
      {"log-anchors", "Tablet Log Anchors"},
      {"transactions", "Transactions"},
      {"rocksdb", "RocksDB" },
      {"waitqueue", "Wait Queue"},
      {"sharedlockmanager", "In-Memory Locks"},
      {"preparer", "Preparer"}};

  auto encoded_tablet_id = UrlEncodeToString(tablet_id);
  for (const auto& entry : entries) {
    *output << Format("<li><a href=\"/$0?id=$2\">$1</a></li>\n",
                      entry[0], entry[1], encoded_tablet_id);
  }

  // End list
  *output << "</ul>\n";
}

void HandleLogAnchorsPage(
    const std::string& tablet_id, const tablet::TabletPeerPtr& peer,
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  *output << "<h1>Log Anchors for Tablet " << EscapeForHtmlToString(tablet_id) << "</h1>"
          << std::endl;

  string dump = peer->log_anchor_registry()->DumpAnchorInfo();
  *output << "<pre>" << EscapeForHtmlToString(dump) << "</pre>" << std::endl;
  std::string retain_op_id_details;
  auto result = peer->GetEarliestNeededLogIndex(&retain_op_id_details);
  *output << "<pre>";
  if (result.ok()) {
    *output << EscapeForHtmlToString(retain_op_id_details);
  } else {
    *output << EscapeForHtmlToString(result.status().ToString());
  }
  *output << "</pre>" << std::endl;
}

void HandleConsensusStatusPage(
    const std::string& tablet_id, const tablet::TabletPeerPtr& peer,
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  auto consensus_result = peer->GetConsensus();
  if (!consensus_result) {
    *output << "Tablet " << EscapeForHtmlToString(tablet_id) << " not running";
    return;
  }

  *output << "<h1>Tablet " << EscapeForHtmlToString(tablet_id) << "</h1>\n";
  consensus_result.get()->DumpStatusHtml(*output);
}

void HandleTransactionsPage(
    const std::string& tablet_id, const tablet::TabletPeerPtr& peer,
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  auto tablet = peer->shared_tablet();
  if (!tablet) {
    *output << "Tablet " << EscapeForHtmlToString(tablet_id) << " not running";
    return;
  }

  *output << "<h1>Transactions for Tablet " << EscapeForHtmlToString(tablet_id) << "</h1>"
          << std::endl;

  auto transaction_participant = tablet->transaction_participant();
  if (transaction_participant) {
    *output << "<pre>" << EscapeForHtmlToString(transaction_participant->DumpTransactions())
            << "</pre>" << std::endl;
    return;
  }

  auto transaction_coordinator = tablet->transaction_coordinator();
  if (transaction_coordinator) {
    *output << "<pre>" << EscapeForHtmlToString(transaction_coordinator->DumpTransactions())
            << "</pre>" << std::endl;
    return;
  }

  *output << "Tablet is non transactional";
}

void DumpRocksDBOptions(rocksdb::DB* db, std::stringstream* out) {
    std::vector<std::string> cf_names;
    std::vector<rocksdb::ColumnFamilyOptions> cf_options;
    db->GetColumnFamiliesOptions(&cf_names, &cf_options);

    auto env = rocksdb::NewMemEnv(db->GetEnv());
    const std::string tag_id = Uuid::Generate().ToHexString();
    *out << "<input type=\"checkbox\" id=\"" << tag_id << "\" class=\"yb-collapsible-cb\"/>"
         << "<label for=\"" << tag_id << "\"><h3>Options</h3></label>" << std::endl
         << "<pre>" << std::endl;

    std::string content;
    auto status = rocksdb::PersistRocksDBOptions(db->GetDBOptions(),
                                                 cf_names, cf_options, "opts", env,
                                                 rocksdb::IncludeHeader::kFalse,
                                                 rocksdb::IncludeFileVersion::kFalse);
    if (PREDICT_TRUE(status.ok())) {
      status = rocksdb::ReadFileToString(env, "opts", &content);
    }
    if (PREDICT_TRUE(status.ok())) {
      EscapeForHtml(content, out);
    } else {
      *out << "Failed to get options: " << EscapeForHtmlToString(status.ToString()) << std::endl;
    }
    *out << "</pre>" << std::endl;
    delete env;
}

void DumpRocksDB(const char* title, rocksdb::DB* db, std::stringstream* out) {
  if (db) {
    *out << "<h2>" << title << "</h2>" << std::endl;
    DumpRocksDBOptions(db, out);

    *out << "<h3>Files</h3>" << std::endl;
    auto files = db->GetLiveFilesMetaData();
    *out << "<pre>" << std::endl;
    for (const auto& file : files) {
      *out << EscapeForHtmlToString(file.ToString()) << std::endl;
    }
    *out << "</pre>" << std::endl;

    rocksdb::TablePropertiesCollection properties;
    auto status = db->GetPropertiesOfAllTables(&properties);
    if (status.ok()) {
      for (const auto& p : properties) {
        *out << "<h3>" << EscapeForHtmlToString(p.first) << " properties</h3>" << std::endl;
        *out << "<pre>" << EscapeForHtmlToString(p.second->ToString("\n")) << "</pre>" << std::endl;
      }
    } else {
      *out << "Failed to get properties: " << EscapeForHtmlToString(status.ToString()) << std::endl;
    }
  }
}

void HandleRocksDBPage(
    const std::string& tablet_id, const tablet::TabletPeerPtr& peer,
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  *output << "<h1>RocksDB for Tablet " << EscapeForHtmlToString(tablet_id) << "</h1>" << std::endl;

  auto tablet_result = peer->shared_tablet_safe();
  if (!tablet_result.ok()) {
    *output << EscapeForHtmlToString(tablet_result.status().ToString());
    return;
  }
  DumpRocksDB("Regular", (*tablet_result)->regular_db(), output);
  DumpRocksDB("Intents", (*tablet_result)->intents_db(), output);
}

void HandleWaitQueuePage(
    const std::string& tablet_id, const tablet::TabletPeerPtr& peer,
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream *out = &resp->output;
  *out << "<h1>Waiters for Tablet " << EscapeForHtmlToString(tablet_id) << "</h1>" << std::endl;

  auto tablet_result = peer->shared_tablet_safe();
  if (!tablet_result.ok()) {
    *out << EscapeForHtmlToString(tablet_result.status().ToString());
    return;
  }
  auto* tp = (*tablet_result)->transaction_participant();
  docdb::WaitQueue* wq = nullptr;
  if (tp) {
    wq = tp->wait_queue();
  }
  if (wq) {
    wq->DumpStatusHtml(*out);
  } else {
    *out << "<h3>" << "No wait queue found" << "</h3>" << std::endl;
  }
}

void HandleInMemoryLocksPage(
    const std::string& tablet_id, const tablet::TabletPeerPtr& peer,
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream *out = &resp->output;
  *out << "<h1>In-Memory Locks for Tablet "
       << EscapeForHtmlToString(tablet_id) << "</h1>" << std::endl;

  auto tablet_result = peer->shared_tablet_safe();
  if (!tablet_result.ok()) {
    *out << EscapeForHtmlToString(tablet_result.status().ToString());
    return;
  }
  auto* shared_lock_manager = (*tablet_result)->shared_lock_manager();
  if (shared_lock_manager) {
    shared_lock_manager->DumpStatusHtml(*out);
  } else {
    *out << "<h3>" << "No shared lock manager found. This is unexpected." << "</h3>" << std::endl;
  }
}

void HandlePreparerPage(
    const std::string& tablet_id, const tablet::TabletPeerPtr& peer,
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream *out = &resp->output;
  *out << "<h1>Prepare Info for Tablet "
       << EscapeForHtmlToString(tablet_id) << "</h1>" << std::endl;

  auto* preparer = DCHECK_NOTNULL(peer->DEBUG_GetPreparer());
  if (preparer) {
    preparer->DumpStatusHtml(*out);
  } else {
    *out << "<h3>" << "No preparer found. This is unexpected." << "</h3>" << std::endl;
  }
}

template<class F>
void RegisterTabletPathHandler(
    Webserver* web_server, TabletServer* tserver, const std::string& path, const F& f) {
  auto handler = [tserver, f](const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
    std::stringstream *output = &resp->output;
    string tablet_id;
    tablet::TabletPeerPtr peer = LoadTablet(tserver, req, &tablet_id, output);
    if (!peer) return;

    f(tablet_id, peer, req, resp);
  };
  web_server->RegisterPathHandler(path, "", handler, true /* styled */, false /* is_on_nav_bar */);
}

}  // namespace

TabletServerPathHandlers::~TabletServerPathHandlers() {
}

Status TabletServerPathHandlers::Register(Webserver* server) {
  server->RegisterPathHandler(
      "/tables", "Tables", std::bind(&TabletServerPathHandlers::HandleTablesPage, this, _1, _2),
      true /* styled */, true /* is_on_nav_bar */, "fa fa-table");
  server->RegisterPathHandler(
      "/tablets", "Tablets", std::bind(&TabletServerPathHandlers::HandleTabletsPage, this, _1, _2),
      true /* styled */, true /* is_on_nav_bar */, "fa fa-server");
  RegisterTabletPathHandler(server, tserver_, "/tablet", &HandleTabletPage);
  server->RegisterPathHandler(
      "/operations", "",
      std::bind(&TabletServerPathHandlers::HandleOperationsPage, this, _1, _2), true /* styled */,
      false /* is_on_nav_bar */);
  server->RegisterPathHandler(
      "/remotebootstraps", "",
      std::bind(&TabletServerPathHandlers::HandleRemoteBootstrapsPage, this, _1, _2),
      true /* styled */,
      false /* is_on_nav_bar */);
  RegisterTabletPathHandler(
      server, tserver_, "/tablet-consensus-status", &HandleConsensusStatusPage);
  RegisterTabletPathHandler(server, tserver_, "/log-anchors", &HandleLogAnchorsPage);
  RegisterTabletPathHandler(server, tserver_, "/transactions", &HandleTransactionsPage);
  RegisterTabletPathHandler(server, tserver_, "/rocksdb", &HandleRocksDBPage);
  RegisterTabletPathHandler(server, tserver_, "/waitqueue", &HandleWaitQueuePage);
  RegisterTabletPathHandler(server, tserver_, "/sharedlockmanager", &HandleInMemoryLocksPage);
  RegisterTabletPathHandler(server, tserver_, "/preparer", &HandlePreparerPage);
  server->RegisterPathHandler(
      "/", "Dashboards",
      std::bind(&TabletServerPathHandlers::HandleDashboardsPage, this, _1, _2), true /* styled */,
      true /* is_on_nav_bar */, "fa fa-dashboard");
#ifndef NDEBUG
  server->RegisterPathHandler(
      "/intentsdb", "IntentsDB",
      std::bind(&TabletServerPathHandlers::HandleIntentsDBPage, this, _1, _2), true /* styled */,
      true /* is_on_nav_bar */, "fa fa-lock");
#endif
  server->RegisterPathHandler(
      "/maintenance-manager", "",
      std::bind(&TabletServerPathHandlers::HandleMaintenanceManagerPage, this, _1, _2),
      true /* styled */, false /* is_on_nav_bar */);
  server->RegisterPathHandler(
      "/xcluster", "xcluster",
      std::bind(&TabletServerPathHandlers::HandleXClusterPage, this, _1, _2), true /* styled */,
      false /* is_on_nav_bar */);

  // APIS.
  server->RegisterPathHandler(
      "/api/v1/health-check", "TServer Health Check",
      std::bind(&TabletServerPathHandlers::HandleHealthCheck, this, _1, _2),
      false /* styled */, false /* is_on_nav_bar */);
  server->RegisterPathHandler(
      "/api/v1/version", "YB Version Information",
      std::bind(&TabletServerPathHandlers::HandleVersionInfoDump, this, _1, _2),
      false /* styled */, false /* is_on_nav_bar */);
  server->RegisterPathHandler(
      "/api/v1/masters", "Master servers and their role (Leader/Follower)",
      std::bind(&TabletServerPathHandlers::HandleListMasterServers, this, _1, _2),
      false /* styled */, false /* is_on_nav_bar */);
  server->RegisterPathHandler(
      "/api/v1/tablets", "Tablets",
      std::bind(&TabletServerPathHandlers::HandleTabletsJSON, this, _1, _2),
      false /* styled */, false /* is_on_nav_bar */);
  server->RegisterPathHandler(
      "/api/v1/meta-cache", "MetaCache",
      std::bind(&TabletServerPathHandlers::HandleTabletMetaCacheJSON, this, _1, _2),
      false /* styled */, false /* is_on_nav_bar */);
  server->RegisterPathHandler(
      "/api/v1/xcluster", "xcluster",
      std::bind(&TabletServerPathHandlers::HandleXClusterJSON, this, _1, _2), false /* styled */,
      false /* is_on_nav_bar */);
  return Status::OK();
}

void TabletServerPathHandlers::HandleVersionInfoDump(const Webserver::WebRequest& req,
                                                    Webserver::WebResponse* resp) {
  // Get the version info.
  VersionInfoPB version_info;
  VersionInfo::GetVersionInfoPB(&version_info);

  std::stringstream *output = &resp->output;
  JsonWriter jw(output, JsonWriter::PRETTY);

  jw.Protobuf(version_info);
}

void TabletServerPathHandlers::HandleOperationsPage(const Webserver::WebRequest& req,
                                                    Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  bool as_text = ContainsKey(req.parsed_args, "raw");

  auto peers = tserver_->tablet_manager()->GetTabletPeers();

  string arg = FindWithDefault(req.parsed_args, "include_traces", "false");
  Operation::TraceType trace_type = ParseLeadingBoolValue(
      arg.c_str(), false) ? Operation::TRACE_TXNS : Operation::NO_TRACE_TXNS;

  if (!as_text) {
    *output << "<h1>Operations</h1>\n";
    *output << "<table class='table table-striped'>\n";
    *output << "   <tr><th>Tablet id</th><th>Op Id</th>"
      "<th>Operation Type</th><th>"
      "Total time in-flight</th><th>Description</th></tr>\n";
  }

  for (const std::shared_ptr<TabletPeer>& peer : peers) {
    vector<OperationStatusPB> inflight;

    auto tablet = peer->shared_tablet();
    if (tablet == nullptr) {
      continue;
    }

    peer->GetInFlightOperations(trace_type, &inflight);
    for (const auto& inflight_tx : inflight) {
      string total_time_str = Substitute("$0 us.", inflight_tx.running_for_micros());
      string description;
      if (trace_type == Operation::TRACE_TXNS) {
        description = Substitute("$0, Trace: $1",
                                  inflight_tx.description(), inflight_tx.trace_buffer());
      } else {
        description = inflight_tx.description();
      }

      if (!as_text) {
        (*output) << Substitute(
          "<tr><th>$0</th><th>$1</th><th>$2</th><th>$3</th><th>$4</th></tr>\n",
          EscapeForHtmlToString(peer->tablet_id()),
          EscapeForHtmlToString(inflight_tx.op_id().ShortDebugString()),
          OperationType_Name(inflight_tx.operation_type()),
          total_time_str,
          EscapeForHtmlToString(description));
      } else {
        (*output) << "Tablet: " << peer->tablet_id() << endl;
        (*output) << "Op ID: " << inflight_tx.op_id().ShortDebugString() << endl;
        (*output) << "Type: " << OperationType_Name(inflight_tx.operation_type()) << endl;
        (*output) << "Running: " << total_time_str;
        (*output) << description << endl;
        (*output) << endl;
      }
    }
  }

  if (!as_text) {
    *output << "</table>\n";
  }
}

void TabletServerPathHandlers::HandleRemoteBootstrapsPage(const Webserver::WebRequest& req,
                                                          Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  auto rbs_service_ptr = tserver_->GetRemoteBootstrapService();
  if (!rbs_service_ptr) {
    *output << "<h2>Could not locate the Remote Bootstrap Service, "
            << "server might be shutting down...</h2>\n";
    return;
  }
  rbs_service_ptr->DumpStatusHtml(*output);
}

namespace {
string TabletLink(const string& id) {
  return Substitute("<a href=\"/tablet?id=$0\">$1</a>",
                    UrlEncodeToString(id),
                    EscapeForHtmlToString(id));
}

bool CompareByTabletId(const std::shared_ptr<TabletPeer>& a,
                       const std::shared_ptr<TabletPeer>& b) {
  return a->tablet_id() < b->tablet_id();
}

string GetOnDiskSizeInHtml(const yb::tablet::TabletOnDiskSizeInfo& info) {
  std::ostringstream disk_size_html;
  disk_size_html << "<ul>"
                 << "<li>" << "Total: "
                 << HumanReadableNumBytes::ToString(info.sum_on_disk_size)
                 << "<li>" << "Consensus Metadata: "
                 << HumanReadableNumBytes::ToString(info.consensus_metadata_disk_size)
                 << "<li>" << "WAL Files: "
                 << HumanReadableNumBytes::ToString(info.wal_files_disk_size)
                 << "<li>" << "SST Files: "
                 << HumanReadableNumBytes::ToString(info.sst_files_disk_size)
                 << "<li>" << "SST Files Uncompressed: "
                 << HumanReadableNumBytes::ToString(info.uncompressed_sst_files_disk_size)
                 << "</ul>";
  return disk_size_html.str();
}

// Returns information about the tables stored on this tablet server.
std::map<TableIdentifier, TableInfo> GetTablesInfo(
    const vector<std::shared_ptr<TabletPeer>>& peers) {
  std::map<TableIdentifier, TableInfo> table_map;

  for (const auto& peer : peers) {
    TabletStatusPB status;
    peer->GetTabletStatusPB(&status);

    const auto tablet_data_state = status.tablet_data_state();
    if (tablet_data_state != TabletDataState::TABLET_DATA_COPYING &&
        tablet_data_state != TabletDataState::TABLET_DATA_READY &&
        tablet_data_state != TabletDataState::TABLET_DATA_SPLIT_COMPLETED) {
      continue;
    }

    auto consensus_result = peer->GetConsensus();
    auto raft_role = PeerRole::UNKNOWN_ROLE;
    if (consensus_result) {
      raft_role = consensus_result.get()->role();
    } else if (status.tablet_data_state() == TabletDataState::TABLET_DATA_COPYING) {
      raft_role = PeerRole::LEARNER;
    }

    auto identifer = TableIdentifier {
      .uuid = std::move(status.table_id()),
      .state = peer->HumanReadableState()
    };

    auto tablet = peer->shared_tablet();
    uint64_t num_sst_files = (tablet) ? tablet->GetCurrentVersionNumSSTFiles() : 0;
    bool is_hidden = status.is_hidden();

    auto info = TabletPeerInfo {
        .namespace_name = std::move(status.namespace_name()),
        .name = std::move(status.table_name()),
        .is_hidden = is_hidden,
        .num_sst_files = num_sst_files,
        .disk_size_info = yb::tablet::TabletOnDiskSizeInfo::FromPB(status),
        .has_on_disk_size = status.has_estimated_on_disk_size(),
        .raft_role = raft_role
    };

    auto table_iter = table_map.find(identifer);
    if (table_iter == table_map.end()) {
      table_map.emplace(identifer, TableInfo(std::move(info)));
    } else {
      table_iter->second.Aggregate(std::move(info));
    }
  }

  return table_map;
}

}  // anonymous namespace

void TabletServerPathHandlers::HandleTablesPage(const Webserver::WebRequest& req,
                                                Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  auto peers = tserver_->tablet_manager()->GetTabletPeers();
  auto table_map = GetTablesInfo(peers);
  bool show_missing_size_footer = false;

  *output << "<h1>Tables</h1>\n"
          << "<table class='table table-striped'>\n"
          << "  <tr>\n"
          << "    <th>Namespace</th><th>Table name</th><th>Table UUID</th>\n"
          << "    <th>State</th><th>Hidden</th><th>Num SST Files</th>\n"
          << "    <th>On-disk size</th><th>Raft roles</th>\n"
          << "  </tr>\n";

  for (const auto& table_iter : table_map) {
    const auto& identifier = table_iter.first;
    const auto& info = table_iter.second;

    string tables_disk_size_html = GetOnDiskSizeInHtml(info.disk_size_info);
    if (!info.has_complete_on_disk_size) {
      tables_disk_size_html += "*";
      show_missing_size_footer = true;
    }

    std::stringstream role_counts_html;
    role_counts_html << "<ul>";
    for (const auto& rc_iter : info.raft_role_counts) {
      role_counts_html << "<li>" << PeerRole_Name(rc_iter.first)
                       << ": " << rc_iter.second << "</li>";
    }
    role_counts_html << "</ul>";

    *output << Substitute(
        "<tr><td>$0</td><td>$1</td><td>$2</td><td>$3</td><td>$4</td>"
        "<td>$5</td><td>$6</td><td>$7</td></tr>\n",
        EscapeForHtmlToString(info.namespace_name),
        EscapeForHtmlToString(info.name),
        EscapeForHtmlToString(identifier.uuid),
        EscapeForHtmlToString(identifier.state),
        info.is_hidden,
        info.num_sst_files,
        tables_disk_size_html,
        role_counts_html.str());
  }

  *output << "</table>\n";

  if (show_missing_size_footer) {
    *output << "<p>* Some tablets did not provide disk size estimates,"
            << " and were not added to the displayed totals.</p>";
  }
}

void TabletServerPathHandlers::HandleTabletsPage(const Webserver::WebRequest& req,
                                                 Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  auto peers = tserver_->tablet_manager()->GetTabletPeers();
  std::sort(peers.begin(), peers.end(), &CompareByTabletId);

  *output << "<h1>Tablets</h1>\n";
  *output << "<table class='table table-striped'>\n";
  *output << "  <tr><th>Namespace</th><th>Table name</th><th>Table UUID</th><th>Tablet ID</th>"
             "<th>Partition</th>"
             "<th>State</th><th>Hidden</th><th>Num SST Files</th><th>On-disk "
             "size</th><th>RaftConfig</th>"
             "<th>Last status</th></tr>\n";
  for (const std::shared_ptr<TabletPeer>& peer : peers) {
    TabletStatusPB status;
    peer->GetTabletStatusPB(&status);
    string id = status.tablet_id();
    string namespace_name = status.namespace_name();
    string table_name = status.table_name();
    string table_id = status.table_id();
    string tablet_id_or_link;
    auto tablet = peer->shared_tablet();
    if (tablet != nullptr) {
      tablet_id_or_link = TabletLink(id);
    } else {
      tablet_id_or_link = EscapeForHtmlToString(id);
    }
    string tablets_disk_size_html = GetOnDiskSizeInHtml(
        yb::tablet::TabletOnDiskSizeInfo::FromPB(status)
    );

    auto tablet_metadata = peer->tablet_metadata();
    string partition = tablet_metadata->partition_schema()
                            ->PartitionDebugString(*peer->status_listener()->partition(),
                                                   *tablet_metadata->schema());

    uint64_t num_sst_files = (tablet) ? tablet->GetCurrentVersionNumSSTFiles() : 0;

    // TODO: would be nice to include some other stuff like memory usage
    auto consensus_result = peer->GetConsensus();
    (*output) << Format(
        // Namespace, Table name, UUID of table, tablet id, partition
        "<tr><td>$0</td><td>$1</td><td>$2</td><td>$3</td><td>$4</td>"
        // State, Hidden, num SST files, on-disk size, consensus configuration, last status
        "<td>$5</td><td>$6</td><td>$7</td><td>$8</td><td>$9</td><td>$10</td></tr>\n",
        EscapeForHtmlToString(namespace_name),              // $0
        EscapeForHtmlToString(table_name),                  // $1
        EscapeForHtmlToString(table_id),                    // $2
        tablet_id_or_link,                                  // $3
        EscapeForHtmlToString(partition),                   // $4
        EscapeForHtmlToString(peer->HumanReadableState()),  // $5
        status.is_hidden(),                                 // $6
        num_sst_files,                                      // $7
        tablets_disk_size_html,                             // $8
        consensus_result ? ConsensusStatePBToHtml(
                               consensus_result.get()->ConsensusState(CONSENSUS_CONFIG_COMMITTED))
                         : "",                         // $9
        EscapeForHtmlToString(status.last_status()));  // $10
  }
  *output << "</table>\n";
}

void TabletServerPathHandlers::HandleListMasterServers(const Webserver::WebRequest& req,
                                                       Webserver::WebResponse* resp) {
  // Get the version info.
  ListMasterServersRequestPB list_masters_req;
  ListMasterServersResponsePB list_masters_resp;

  const Status s = tserver_->ListMasterServers(&list_masters_req, &list_masters_resp);

  std::stringstream *output = &resp->output;
  JsonWriter jw(output, JsonWriter::PRETTY);
  jw.Protobuf(list_masters_resp);
}

namespace {

bool CompareByMemberType(const RaftPeerPB& a, const RaftPeerPB& b) {
  if (!a.has_member_type()) return false;
  if (!b.has_member_type()) return true;
  return a.member_type() < b.member_type();
}

}  // anonymous namespace

string TabletServerPathHandlers::ConsensusStatePBToHtml(const ConsensusStatePB& cstate) const {
  std::stringstream html;

  html << "<ul>\n";
  std::vector<RaftPeerPB> sorted_peers;
  sorted_peers.assign(cstate.config().peers().begin(), cstate.config().peers().end());
  std::sort(sorted_peers.begin(), sorted_peers.end(), &CompareByMemberType);
  for (const RaftPeerPB& peer : sorted_peers) {
    std::string peer_addr_or_uuid = !peer.last_known_private_addr().empty()
        ? peer.last_known_private_addr()[0].host()
        : peer.permanent_uuid();
    peer_addr_or_uuid = EscapeForHtmlToString(peer_addr_or_uuid);
    string role_name = PeerRole_Name(GetConsensusRole(peer.permanent_uuid(), cstate));
    string formatted = Substitute("$0: $1", role_name, peer_addr_or_uuid);
    // Make the local peer bold.
    if (peer.permanent_uuid() == tserver_->instance_pb().permanent_uuid()) {
      formatted = Substitute("<b>$0</b>", formatted);
    }

    html << Substitute(" <li>$0</li>\n", formatted);
  }
  html << "</ul>\n";
  return html.str();
}

void TabletServerPathHandlers::HandleDashboardsPage(const Webserver::WebRequest& req,
                                                    Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  *output << "<h3>Dashboards</h3>\n";
  *output << "<table class='table table-striped'>\n";
  *output << "  <tr><th>Dashboard</th><th>Description</th></tr>\n";
  *output << GetDashboardLine(
      "operations", "Operations", "List of operations that are currently replicating.");
  *output << GetDashboardLine(
      "remotebootstraps", "Remote Bootstraps Sessions being served",
      "List of remote bootstrap sessions this tablet server is currently serving.");
  *output << GetDashboardLine("maintenance-manager", "Maintenance Manager",
                              "List of operations that are currently running and those "
                              "that are registered.");
}

void TabletServerPathHandlers::HandleIntentsDBPage(const Webserver::WebRequest& req,
                                                   Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  if (!FLAGS_enable_intentsdb_page) {
    (*output) << "Intentsdb is disabled, please set the gflag enable_intentsdb_page to true "
                 "to show the content of intentsdb.";
    return;
  }

  TSTabletManager::TabletPtrs tablet_ptrs;
  std::vector<tablet::TabletPeerPtr> tablet_peers;

  auto it = req.parsed_args.find("tablet_id");
  if (it != req.parsed_args.end()) {
    auto tablet_id = it->second;
    auto tablet_peer = LookupTabletPeer(tserver_->tablet_peer_lookup(), tablet_id);
    if (!tablet_peer.ok()) {
      (*output) << Format("Unable to lookup tablet with ID {}", tablet_id);
      return;
    }
    auto tablet = tablet_peer.get().tablet;
    if (tablet == nullptr) {
      (*output) << Format("Unable to lookup tablet with ID {}", tablet_id);
      return;
    }
    tablet_ptrs.push_back(std::move(tablet));
  } else {
    tablet_peers = tserver_->tablet_manager()->GetTabletPeers(&tablet_ptrs);
  }

  std::vector<std::string> intents;
  for (const auto& tablet : tablet_ptrs) {
    auto res = tablet->ReadIntents(&intents);
    if (!res.ok()) {
      (*output) << "Got an error when reading intents";
      return;
    }
  }

  (*output) << Format("Intents size: $0<br>", intents.size()) << "\n";
  for (const auto& item : intents) {
    (*output) << Format("$0<br>", EscapeForHtmlToString(item));
  }
}

string TabletServerPathHandlers::GetDashboardLine(const std::string& link,
                                                  const std::string& text,
                                                  const std::string& desc) {
  return Substitute("  <tr><td><a href=\"$0\">$1</a></td><td>$2</td></tr>\n",
                    EscapeForHtmlToString(link),
                    EscapeForHtmlToString(text),
                    EscapeForHtmlToString(desc));
}

void TabletServerPathHandlers::HandleMaintenanceManagerPage(const Webserver::WebRequest& req,
                                                            Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  MaintenanceManager* manager = tserver_->maintenance_manager();
  MaintenanceManagerStatusPB pb;
  manager->GetMaintenanceManagerStatusDump(&pb);
  if (ContainsKey(req.parsed_args, "raw")) {
    *output << pb.DebugString();
    return;
  }

  int ops_count = pb.registered_operations_size();

  *output << "<h1>Maintenance Manager state</h1>\n";
  *output << "<h3>Running operations</h3>\n";
  *output << "<table class='table table-striped'>\n";
  *output << "  <tr><th>Name</th><th>Instances running</th></tr>\n";
  for (int i = 0; i < ops_count; i++) {
    MaintenanceManagerStatusPB_MaintenanceOpPB op_pb = pb.registered_operations(i);
    if (op_pb.running() > 0) {
      *output <<  Substitute("<tr><td>$0</td><td>$1</td></tr>\n",
                             EscapeForHtmlToString(op_pb.name()),
                             op_pb.running());
    }
  }
  *output << "</table>\n";

  *output << "<h3>Recent completed operations</h3>\n";
  *output << "<table class='table table-striped'>\n";
  *output << "  <tr><th>Name</th><th>Duration</th><th>Time since op started</th></tr>\n";
  for (int i = 0; i < pb.completed_operations_size(); i++) {
    MaintenanceManagerStatusPB_CompletedOpPB op_pb = pb.completed_operations(i);
    *output <<  Substitute("<tr><td>$0</td><td>$1</td><td>$2</td></tr>\n",
                           EscapeForHtmlToString(op_pb.name()),
                           HumanReadableElapsedTime::ToShortString(
                               op_pb.duration_millis() / 1000.0),
                           HumanReadableElapsedTime::ToShortString(
                               op_pb.secs_since_start()));
  }
  *output << "</table>\n";

  *output << "<h3>Non-running operations</h3>\n";
  *output << "<table class='table table-striped'>\n";
  *output << "  <tr><th>Name</th><th>Runnable</th><th>RAM anchored</th>\n"
          << "       <th>Logs retained</th><th>Perf</th></tr>\n";
  for (int i = 0; i < ops_count; i++) {
    MaintenanceManagerStatusPB_MaintenanceOpPB op_pb = pb.registered_operations(i);
    if (op_pb.running() == 0) {
      *output << Substitute("<tr><td>$0</td><td>$1</td><td>$2</td><td>$3</td><td>$4</td></tr>\n",
                            EscapeForHtmlToString(op_pb.name()),
                            op_pb.runnable(),
                            HumanReadableNumBytes::ToString(op_pb.ram_anchored_bytes()),
                            HumanReadableNumBytes::ToString(op_pb.logs_retained_bytes()),
                            op_pb.perf_improvement());
    }
  }
  *output << "</table>\n";
}

namespace {

std::vector<xrepl::StreamTabletStats> GetXClusterOutboundStreamStats(TabletServer* const tserver) {
  auto cdc_service = tserver->GetCDCService();
  if (!cdc_service || !cdc_service->CDCEnabled()) {
    return {};
  }
  auto stream_tablet_stats = cdc_service->GetAllStreamTabletStats();
  if (stream_tablet_stats.empty()) {
    return {};
  }

  xrepl::StreamTabletStats agg_stats;
  agg_stats.stream_id_str = "[Aggregate]";

  std::sort(stream_tablet_stats.begin(), stream_tablet_stats.end());
  for (const auto& stat : stream_tablet_stats) {
    agg_stats += stat;
  }
  agg_stats /= stream_tablet_stats.size();
  stream_tablet_stats.emplace_back(std::move(agg_stats));

  return stream_tablet_stats;
}

std::vector<XClusterPollerStats> GetXClusterInboundStreamStats(TabletServer* const tserver) {
  auto* xcluster_consumer = tserver->GetXClusterConsumer();
  if (!xcluster_consumer) {
    return {};
  }

  auto pollers_stats = xcluster_consumer->GetPollerStats();
  if (pollers_stats.empty()) {
    return pollers_stats;
  }

  std::sort(pollers_stats.begin(), pollers_stats.end());
  XClusterPollerStats agg_stats("[Aggregate]");
  for (const auto& stat : pollers_stats) {
    agg_stats += stat;
  }
  agg_stats /= pollers_stats.size();
  pollers_stats.emplace_back(std::move(agg_stats));
  return pollers_stats;
}
}  // anonymous namespace

void TabletServerPathHandlers::HandleXClusterPage(
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream& output = resp->output;

  if (!FLAGS_enable_xcluster_stat_collection) {
    output << "<h3>xCluster stats collection is not enabled. Set enable_xcluster_stat_collection "
               "to enable it.</h3 >\n";
    return;
  }

  const auto xcluster_outbound_stream_stats = GetXClusterOutboundStreamStats(tserver_);
  const auto xcluster_inbound_stream_stats = GetXClusterInboundStreamStats(tserver_);
  if (xcluster_outbound_stream_stats.empty() && xcluster_inbound_stream_stats.empty()) {
    output << "<h3>xCluster replication is not enabled</h3 >\n";
    return;
  }

  output << "<h1>xCluster state</h1>\n";

  if (!xcluster_outbound_stream_stats.empty()) {
    output << "<h3>xCluster outbound streams</h3>\n";

    HTML_PRINT_TABLE_WITH_HEADER_ROW(
        xcluster_streams, "Stream Id", "Produce Table Id", "Producer Tablet Id", "State",
        "Avg poll delay (ms)", "Throughput (KiBps)", "Data sent (MiB)", "Records sent",
        "Avg GetChanges latency (ms)", "WAL index sent", "WAL end index", "Last poll at", "Status");

    for (const auto& stat : xcluster_outbound_stream_stats) {
      HTML_PRINT_TABLE_ROW(
          stat.stream_id_str, stat.producer_table_id, stat.producer_tablet_id, stat.state,
          stat.avg_poll_delay_ms, StringPrintf("%.3f", stat.avg_throughput_kbps),
          StringPrintf("%.3f", stat.mbs_sent), stat.records_sent, stat.avg_get_changes_latency_ms,
          stat.sent_index, stat.latest_index, stat.last_poll_time.ToFormattedString(), stat.status);
    }

    HTML_END_TABLE;
  }

  if (!xcluster_inbound_stream_stats.empty()) {
    output << "<h3>xCluster inbound streams</h3>\n";

    HTML_PRINT_TABLE_WITH_HEADER_ROW(
        xcluster_pollers, "ReplicationGroup Id", "Stream Id", "Consumer Table Id",
        "Consumer Tablet Id", "Producer Tablet Id", "State", "Avg poll delay (ms)",
        "Throughput (KiBps)", "Data received (MiB)", "Records received",
        "Avg GetChanges latency (ms)", "Avg apply latency (ms)", "WAL index received",
        "Last poll At", "Status");

    for (const auto& stat : xcluster_inbound_stream_stats) {
      HTML_PRINT_TABLE_ROW(
          stat.replication_group_id, stat.stream_id_str, stat.consumer_table_id,
          stat.consumer_tablet_id, stat.producer_tablet_id, stat.state, stat.avg_poll_delay_ms,
          StringPrintf("%.3f", stat.avg_throughput_kbps), StringPrintf("%.3f", stat.mbs_received),
          stat.records_received, stat.avg_get_changes_latency_ms, stat.avg_apply_latency_ms,
          stat.received_index, stat.last_poll_time.ToFormattedString(), stat.status);
    }

    HTML_END_TABLE;
  }

  output << "\n<aside><h5>Note:</h5><p>This data is collected over the last few polls. Check "
             "metrics or logs for older and detailed information.</p></aside>";
  HTML_ADD_SORT_AND_FILTER_TABLE_SCRIPT;
}

void TabletServerPathHandlers::HandleXClusterJSON(
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  if (!FLAGS_enable_xcluster_stat_collection) {
    return;
  }

  std::stringstream* output = &resp->output;
  const auto xcluster_outbound_stream_stats = GetXClusterOutboundStreamStats(tserver_);
  const auto xcluster_inbound_stream_stats = GetXClusterInboundStreamStats(tserver_);

  JsonWriter jw(output, JsonWriter::COMPACT);

  jw.StartObject();
  if (!xcluster_outbound_stream_stats.empty()) {
    jw.String("outbound_streams");
    jw.StartArray();

    for (const auto& stat : xcluster_outbound_stream_stats) {
      jw.StartObject();
      jw.String("stream_id");
      jw.String(stat.stream_id_str);
      jw.String("producer_table_id");
      jw.String(stat.producer_table_id);
      jw.String("producer_tablet_id");
      jw.String(stat.producer_tablet_id);
      jw.String("state");
      jw.String(stat.state);
      jw.String("avg_poll_delay_ms");
      jw.Uint64(stat.avg_poll_delay_ms);
      jw.String("avg_throughput_KiBps");
      jw.Double(stat.avg_throughput_kbps);
      jw.String("MiBs_sent");
      jw.Double(stat.mbs_sent);
      jw.String("records_sent");
      jw.Uint64(stat.records_sent);
      jw.String("avg_get_changes_latency_ms");
      jw.Uint64(stat.avg_get_changes_latency_ms);
      jw.String("wal_sent_index");
      jw.Int64(stat.sent_index);
      jw.String("wal_end_index");
      jw.Int64(stat.latest_index);
      jw.String("last_poll_time");
      jw.String(stat.last_poll_time.ToFormattedString());
      jw.String("status");
      jw.String(stat.status.ToString());
      jw.EndObject();
    }
    jw.EndArray();
  }

  if (!xcluster_inbound_stream_stats.empty()) {
    jw.String("inbound_streams");
    jw.StartArray();
    for (const auto& stat : xcluster_inbound_stream_stats) {
      jw.StartObject();
      jw.String("replication_group_id");
      jw.String(stat.replication_group_id.ToString());
      jw.String("stream_id");
      jw.String(stat.stream_id_str);
      jw.String("consumer_table_id");
      jw.String(stat.consumer_table_id);
      jw.String("consumer_tablet_id");
      jw.String(stat.consumer_tablet_id);
      jw.String("producer_tablet_id");
      jw.String(stat.producer_tablet_id);
      jw.String("state");
      jw.String(stat.state);
      jw.String("avg_poll_delay_ms");
      jw.Uint64(stat.avg_poll_delay_ms);
      jw.String("avg_throughput_KiBps");
      jw.Double(stat.avg_throughput_kbps);
      jw.String("MiBs_received");
      jw.Double(stat.mbs_received);
      jw.String("records_received");
      jw.Uint64(stat.records_received);
      jw.String("avg_get_changes_latency_ms");
      jw.Uint64(stat.avg_get_changes_latency_ms);
      jw.String("avg_apply_latency_ms");
      jw.Uint64(stat.avg_apply_latency_ms);
      jw.String("received_index");
      jw.Int64(stat.received_index);
      jw.String("last_poll_time");
      jw.String(stat.last_poll_time.ToFormattedString());
      jw.String("status");
      jw.String(stat.status.ToString());
      jw.EndObject();
    }
    jw.EndArray();
  }
  jw.EndObject();
}

void TabletServerPathHandlers::HandleHealthCheck(const Webserver::WebRequest& req,
                                                 Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  JsonWriter jw(output, JsonWriter::COMPACT);
  auto tablet_peers = tserver_->tablet_manager()->GetTabletPeers();

  jw.StartObject();
  jw.String("failed_tablets");
  jw.StartArray();
  for (const auto& peer : tablet_peers) {
    if (peer->state() == tablet::FAILED) {
      jw.String(peer->tablet_id());
    }
  }
  jw.EndArray();
  jw.EndObject();
}

void TabletServerPathHandlers::HandleTabletsJSON(const Webserver::WebRequest& req,
                                                 Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  JsonWriter jw(output, JsonWriter::COMPACT);

  auto peers = tserver_->tablet_manager()->GetTabletPeers();
  std::sort(peers.begin(), peers.end(), &CompareByTabletId);
  jw.StartObject();

  for (const std::shared_ptr<TabletPeer>& peer : peers) {
    TabletStatusPB status;
    peer->GetTabletStatusPB(&status);
    string id = status.tablet_id();
    const auto& tablet = peer->shared_tablet();
    string tablets_disk_size_html = GetOnDiskSizeInHtml(
        yb::tablet::TabletOnDiskSizeInfo::FromPB(status)
    );

    const auto& tablet_metadata = peer->tablet_metadata();
    string partition = tablet_metadata->partition_schema()
                            ->PartitionDebugString(*peer->status_listener()->partition(),
                                                   *tablet_metadata->schema());

    uint64_t num_sst_files = (tablet) ? tablet->GetCurrentVersionNumSSTFiles() : 0;

    // TODO: Would be nice to include some other stuff like memory usage.
    const auto& consensus = peer->GetConsensus();

    jw.String(id);
    jw.StartObject();
    jw.String("namespace");
    jw.String(status.namespace_name());
    jw.String("table_name");
    jw.String(status.table_name());
    jw.String("table_id");
    jw.String(status.table_id());
    jw.String("partition");
    jw.String(partition);
    jw.String("state");
    jw.String(peer->HumanReadableState());
    jw.String("hidden");
    jw.Bool(status.is_hidden());
    jw.String("num_sst_files");
    jw.Uint64(num_sst_files);
    jw.String("on_disk_size");

    jw.StartObject();
    const yb::tablet::TabletOnDiskSizeInfo& info = yb::tablet::TabletOnDiskSizeInfo::FromPB(status);
    jw.String("total_size");
    jw.String(HumanReadableNumBytes::ToString(info.sum_on_disk_size));
    jw.String("total_size_bytes");
    jw.Uint64(info.sum_on_disk_size);
    jw.String("consensus_metadata_size");
    jw.String(HumanReadableNumBytes::ToString(info.consensus_metadata_disk_size));
    jw.String("consensus_metadata_size_bytes");
    jw.Uint64(info.consensus_metadata_disk_size);
    jw.String("wal_files_size");
    jw.String(HumanReadableNumBytes::ToString(info.wal_files_disk_size));
    jw.String("wal_files_size_bytes");
    jw.Uint64(info.wal_files_disk_size);
    jw.String("sst_files_size");
    jw.String(HumanReadableNumBytes::ToString(info.sst_files_disk_size));
    jw.String("sst_files_size_bytes");
    jw.Uint64(info.sst_files_disk_size);
    jw.String("uncompressed_sst_files_size");
    jw.String(HumanReadableNumBytes::ToString(info.uncompressed_sst_files_disk_size));
    jw.String("uncompressed_sst_files_size_bytes");
    jw.Uint64(info.uncompressed_sst_files_disk_size);
    jw.EndObject();

    jw.String("raft_config");
    jw.StartArray();
    if (consensus) {
        const ConsensusStatePB& cstate =
            consensus.get()->ConsensusState(CONSENSUS_CONFIG_COMMITTED);
        std::vector<RaftPeerPB> sorted_peers;
        sorted_peers.assign(cstate.config().peers().begin(), cstate.config().peers().end());
        std::sort(sorted_peers.begin(), sorted_peers.end(), &CompareByMemberType);
        for (const RaftPeerPB& peer : sorted_peers) {
          jw.StartObject();
          std::string peer_addr_or_uuid = !peer.last_known_private_addr().empty()
              ? peer.last_known_private_addr()[0].host()
              : peer.permanent_uuid();
          peer_addr_or_uuid = EscapeForHtmlToString(peer_addr_or_uuid);
          string role_name = PeerRole_Name(GetConsensusRole(peer.permanent_uuid(), cstate));
          jw.String(role_name);
          jw.String(peer_addr_or_uuid);
          jw.EndObject();
        }
    }
    jw.EndArray();
    jw.String("status");
    jw.String(status.last_status());
    jw.EndObject();
  }
  jw.EndObject();
}

void TabletServerPathHandlers::HandleTabletMetaCacheJSON(
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream* output = &resp->output;
  JsonWriter writer(output, JsonWriter::COMPACT);
  tserver_->WriteServerMetaCacheAsJson(&writer);
}

}  // namespace tserver
}  // namespace yb
