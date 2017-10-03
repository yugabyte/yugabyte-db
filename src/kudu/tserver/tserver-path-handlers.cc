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

#include "kudu/tserver/tserver-path-handlers.h"

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "kudu/consensus/log_anchor_registry.h"
#include "kudu/consensus/quorum_util.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/strings/human_readable.h"
#include "kudu/gutil/strings/join.h"
#include "kudu/gutil/strings/numbers.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/server/webui_util.h"
#include "kudu/tablet/maintenance_manager.h"
#include "kudu/tablet/tablet.pb.h"
#include "kudu/tablet/tablet_bootstrap.h"
#include "kudu/tablet/tablet_peer.h"
#include "kudu/tserver/scanners.h"
#include "kudu/tserver/tablet_server.h"
#include "kudu/tserver/ts_tablet_manager.h"
#include "kudu/util/url-coding.h"

using kudu::consensus::GetConsensusRole;
using kudu::consensus::CONSENSUS_CONFIG_COMMITTED;
using kudu::consensus::ConsensusStatePB;
using kudu::consensus::RaftPeerPB;
using kudu::consensus::TransactionStatusPB;
using kudu::tablet::MaintenanceManagerStatusPB;
using kudu::tablet::MaintenanceManagerStatusPB_CompletedOpPB;
using kudu::tablet::MaintenanceManagerStatusPB_MaintenanceOpPB;
using kudu::tablet::Tablet;
using kudu::tablet::TabletPeer;
using kudu::tablet::TabletStatusPB;
using kudu::tablet::Transaction;
using std::endl;
using std::shared_ptr;
using std::vector;
using strings::Substitute;

namespace kudu {
namespace tserver {

TabletServerPathHandlers::~TabletServerPathHandlers() {
}

Status TabletServerPathHandlers::Register(Webserver* server) {
  server->RegisterPathHandler(
    "/scans", "Scans",
    boost::bind(&TabletServerPathHandlers::HandleScansPage, this, _1, _2),
    true /* styled */, false /* is_on_nav_bar */);
  server->RegisterPathHandler(
    "/tablets", "Tablets",
    boost::bind(&TabletServerPathHandlers::HandleTabletsPage, this, _1, _2),
    true /* styled */, true /* is_on_nav_bar */);
  server->RegisterPathHandler(
    "/tablet", "",
    boost::bind(&TabletServerPathHandlers::HandleTabletPage, this, _1, _2),
    true /* styled */, false /* is_on_nav_bar */);
  server->RegisterPathHandler(
    "/transactions", "",
    boost::bind(&TabletServerPathHandlers::HandleTransactionsPage, this, _1, _2),
    true /* styled */, false /* is_on_nav_bar */);
  server->RegisterPathHandler(
    "/tablet-rowsetlayout-svg", "",
    boost::bind(&TabletServerPathHandlers::HandleTabletSVGPage, this, _1, _2),
    true /* styled */, false /* is_on_nav_bar */);
  server->RegisterPathHandler(
    "/tablet-consensus-status", "",
    boost::bind(&TabletServerPathHandlers::HandleConsensusStatusPage, this, _1, _2),
    true /* styled */, false /* is_on_nav_bar */);
  server->RegisterPathHandler(
    "/log-anchors", "",
    boost::bind(&TabletServerPathHandlers::HandleLogAnchorsPage, this, _1, _2),
    true /* styled */, false /* is_on_nav_bar */);
  server->RegisterPathHandler(
    "/dashboards", "Dashboards",
    boost::bind(&TabletServerPathHandlers::HandleDashboardsPage, this, _1, _2),
    true /* styled */, true /* is_on_nav_bar */);
  server->RegisterPathHandler(
    "/maintenance-manager", "",
    boost::bind(&TabletServerPathHandlers::HandleMaintenanceManagerPage, this, _1, _2),
    true /* styled */, false /* is_on_nav_bar */);

  return Status::OK();
}

void TabletServerPathHandlers::HandleTransactionsPage(const Webserver::WebRequest& req,
                                                      std::stringstream* output) {
  bool as_text = ContainsKey(req.parsed_args, "raw");

  vector<scoped_refptr<TabletPeer> > peers;
  tserver_->tablet_manager()->GetTabletPeers(&peers);

  string arg = FindWithDefault(req.parsed_args, "include_traces", "false");
  Transaction::TraceType trace_type = ParseLeadingBoolValue(
      arg.c_str(), false) ? Transaction::TRACE_TXNS : Transaction::NO_TRACE_TXNS;

  if (!as_text) {
    *output << "<h1>Transactions</h1>\n";
    *output << "<table class='table table-striped'>\n";
    *output << "   <tr><th>Tablet id</th><th>Op Id</th>"
      "<th>Transaction Type</th><th>"
      "Total time in-flight</th><th>Description</th></tr>\n";
  }

  for (const scoped_refptr<TabletPeer>& peer : peers) {
    vector<TransactionStatusPB> inflight;

    if (peer->tablet() == nullptr) {
      continue;
    }

    peer->GetInFlightTransactions(trace_type, &inflight);
    for (const TransactionStatusPB& inflight_tx : inflight) {
      string total_time_str = Substitute("$0 us.", inflight_tx.running_for_micros());
      string description;
      if (trace_type == Transaction::TRACE_TXNS) {
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
          OperationType_Name(inflight_tx.tx_type()),
          total_time_str,
          EscapeForHtmlToString(description));
      } else {
        (*output) << "Tablet: " << peer->tablet_id() << endl;
        (*output) << "Op ID: " << inflight_tx.op_id().ShortDebugString() << endl;
        (*output) << "Type: " << OperationType_Name(inflight_tx.tx_type()) << endl;
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

namespace {
string TabletLink(const string& id) {
  return Substitute("<a href=\"/tablet?id=$0\">$1</a>",
                    UrlEncodeToString(id),
                    EscapeForHtmlToString(id));
}

bool CompareByTabletId(const scoped_refptr<TabletPeer>& a,
                       const scoped_refptr<TabletPeer>& b) {
  return a->tablet_id() < b->tablet_id();
}

} // anonymous namespace

void TabletServerPathHandlers::HandleTabletsPage(const Webserver::WebRequest& req,
                                                 std::stringstream *output) {
  vector<scoped_refptr<TabletPeer> > peers;
  tserver_->tablet_manager()->GetTabletPeers(&peers);
  std::sort(peers.begin(), peers.end(), &CompareByTabletId);

  *output << "<h1>Tablets</h1>\n";
  *output << "<table class='table table-striped'>\n";
  *output << "  <tr><th>Table name</th><th>Tablet ID</th>"
      "<th>Partition</th>"
      "<th>State</th><th>On-disk size</th><th>RaftConfig</th><th>Last status</th></tr>\n";
  for (const scoped_refptr<TabletPeer>& peer : peers) {
    TabletStatusPB status;
    peer->GetTabletStatusPB(&status);
    string id = status.tablet_id();
    string table_name = status.table_name();
    string tablet_id_or_link;
    if (peer->tablet() != nullptr) {
      tablet_id_or_link = TabletLink(id);
    } else {
      tablet_id_or_link = EscapeForHtmlToString(id);
    }
    string n_bytes = "";
    if (status.has_estimated_on_disk_size()) {
      n_bytes = HumanReadableNumBytes::ToString(status.estimated_on_disk_size());
    }
    string partition = peer->tablet_metadata()
                           ->partition_schema()
                            .PartitionDebugString(peer->status_listener()->partition(),
                                                  peer->tablet_metadata()->schema());

    // TODO: would be nice to include some other stuff like memory usage
    scoped_refptr<consensus::Consensus> consensus = peer->shared_consensus();
    (*output) << Substitute(
        // Table name, tablet id, partition
        "<tr><td>$0</td><td>$1</td><td>$2</td>"
        // State, on-disk size, consensus configuration, last status
        "<td>$3</td><td>$4</td><td>$5</td><td>$6</td></tr>\n",
        EscapeForHtmlToString(table_name), // $0
        tablet_id_or_link, // $1
        EscapeForHtmlToString(partition), // $2
        EscapeForHtmlToString(peer->HumanReadableState()), n_bytes, // $3, $4
        consensus ? ConsensusStatePBToHtml(consensus->ConsensusState(CONSENSUS_CONFIG_COMMITTED))
                  : "", // $5
        EscapeForHtmlToString(status.last_status())); // $6
  }
  *output << "</table>\n";
}

namespace {

bool CompareByMemberType(const RaftPeerPB& a, const RaftPeerPB& b) {
  if (!a.has_member_type()) return false;
  if (!b.has_member_type()) return true;
  return a.member_type() < b.member_type();
}

} // anonymous namespace

string TabletServerPathHandlers::ConsensusStatePBToHtml(const ConsensusStatePB& cstate) const {
  std::stringstream html;

  html << "<ul>\n";
  std::vector<RaftPeerPB> sorted_peers;
  sorted_peers.assign(cstate.config().peers().begin(), cstate.config().peers().end());
  std::sort(sorted_peers.begin(), sorted_peers.end(), &CompareByMemberType);
  for (const RaftPeerPB& peer : sorted_peers) {
    string peer_addr_or_uuid =
        peer.has_last_known_addr() ? peer.last_known_addr().host() : peer.permanent_uuid();
    peer_addr_or_uuid = EscapeForHtmlToString(peer_addr_or_uuid);
    string role_name = RaftPeerPB::Role_Name(GetConsensusRole(peer.permanent_uuid(), cstate));
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

namespace {

bool GetTabletID(const Webserver::WebRequest& req, string* id, std::stringstream *out) {
  if (!FindCopy(req.parsed_args, "id", id)) {
    // TODO: webserver should give a way to return a non-200 response code
    (*out) << "Tablet missing 'id' argument";
    return false;
  }
  return true;
}

bool GetTabletPeer(TabletServer* tserver, const Webserver::WebRequest& req,
                   scoped_refptr<TabletPeer>* peer, const string& tablet_id,
                   std::stringstream *out) {
  if (!tserver->tablet_manager()->LookupTablet(tablet_id, peer)) {
    (*out) << "Tablet " << EscapeForHtmlToString(tablet_id) << " not found";
    return false;
  }
  return true;
}

bool TabletBootstrapping(const scoped_refptr<TabletPeer>& peer, const string& tablet_id,
                         std::stringstream* out) {
  if (peer->state() == tablet::BOOTSTRAPPING) {
    (*out) << "Tablet " << EscapeForHtmlToString(tablet_id) << " is still bootstrapping";
    return false;
  }
  return true;
}

// Returns true if the tablet_id was properly specified, the
// tablet is found, and is in a non-bootstrapping state.
bool LoadTablet(TabletServer* tserver,
                const Webserver::WebRequest& req,
                string* tablet_id, scoped_refptr<TabletPeer>* peer,
                std::stringstream* out) {
  if (!GetTabletID(req, tablet_id, out)) return false;
  if (!GetTabletPeer(tserver, req, peer, *tablet_id, out)) return false;
  if (!TabletBootstrapping(*peer, *tablet_id, out)) return false;
  return true;
}

} // anonymous namespace

void TabletServerPathHandlers::HandleTabletPage(const Webserver::WebRequest& req,
                                                std::stringstream *output) {
  string tablet_id;
  scoped_refptr<TabletPeer> peer;
  if (!LoadTablet(tserver_, req, &tablet_id, &peer, output)) return;

  string table_name = peer->tablet_metadata()->table_name();

  *output << "<h1>Tablet " << EscapeForHtmlToString(tablet_id) << "</h1>\n";

  // Output schema in tabular format.
  *output << "<h2>Schema</h2>\n";
  const Schema& schema = peer->tablet_metadata()->schema();
  HtmlOutputSchemaTable(schema, output);

  *output << "<h2>Other Tablet Info Pages</h2>" << endl;

  // List of links to various tablet-specific info pages
  *output << "<ul>";

  // Link to output svg of current DiskRowSet layout over keyspace.
  *output << "<li>" << Substitute("<a href=\"/tablet-rowsetlayout-svg?id=$0\">$1</a>",
                                  UrlEncodeToString(tablet_id),
                                  "Rowset Layout Diagram")
          << "</li>" << endl;

  // Link to consensus status page.
  *output << "<li>" << Substitute("<a href=\"/tablet-consensus-status?id=$0\">$1</a>",
                                  UrlEncodeToString(tablet_id),
                                  "Consensus Status")
          << "</li>" << endl;

  // Log anchors info page.
  *output << "<li>" << Substitute("<a href=\"/log-anchors?id=$0\">$1</a>",
                                  UrlEncodeToString(tablet_id),
                                  "Tablet Log Anchors")
          << "</li>" << endl;

  // End list
  *output << "</ul>\n";
}

void TabletServerPathHandlers::HandleTabletSVGPage(const Webserver::WebRequest& req,
                                                   std::stringstream* output) {
  string id;
  scoped_refptr<TabletPeer> peer;
  if (!LoadTablet(tserver_, req, &id, &peer, output)) return;
  shared_ptr<Tablet> tablet = peer->shared_tablet();
  if (!tablet) {
    *output << "Tablet " << EscapeForHtmlToString(id) << " not running";
    return;
  }

  *output << "<h1>Rowset Layout Diagram for Tablet "
          << TabletLink(id) << "</h1>\n";
  tablet->PrintRSLayout(output);

}

void TabletServerPathHandlers::HandleLogAnchorsPage(const Webserver::WebRequest& req,
                                                    std::stringstream* output) {
  string tablet_id;
  scoped_refptr<TabletPeer> peer;
  if (!LoadTablet(tserver_, req, &tablet_id, &peer, output)) return;

  *output << "<h1>Log Anchors for Tablet " << EscapeForHtmlToString(tablet_id) << "</h1>"
          << std::endl;

  string dump = peer->log_anchor_registry()->DumpAnchorInfo();
  *output << "<pre>" << EscapeForHtmlToString(dump) << "</pre>" << std::endl;
}

void TabletServerPathHandlers::HandleConsensusStatusPage(const Webserver::WebRequest& req,
                                                         std::stringstream* output) {
  string id;
  scoped_refptr<TabletPeer> peer;
  if (!LoadTablet(tserver_, req, &id, &peer, output)) return;
  scoped_refptr<consensus::Consensus> consensus = peer->shared_consensus();
  if (!consensus) {
    *output << "Tablet " << EscapeForHtmlToString(id) << " not running";
    return;
  }
  consensus->DumpStatusHtml(*output);
}

void TabletServerPathHandlers::HandleScansPage(const Webserver::WebRequest& req,
                                               std::stringstream* output) {
  *output << "<h1>Scans</h1>\n";
  *output << "<table class='table table-striped'>\n";
  *output << "<tr><th>Tablet id</th><th>Scanner id</th><th>Total time in-flight</th>"
      "<th>Time since last update</th><th>Requestor</th><th>Iterator Stats</th>"
      "<th>Pushed down key predicates</th><th>Other predicates</th></tr>\n";

  vector<SharedScanner> scanners;
  tserver_->scanner_manager()->ListScanners(&scanners);
  for (const SharedScanner& scanner : scanners) {
    *output << ScannerToHtml(*scanner);
  }
  *output << "</table>";
}

string TabletServerPathHandlers::ScannerToHtml(const Scanner& scanner) const {
  std::stringstream html;
  uint64_t time_in_flight_us =
      MonoTime::Now(MonoTime::COARSE).GetDeltaSince(scanner.start_time()).ToMicroseconds();
  uint64_t time_since_last_access_us =
      scanner.TimeSinceLastAccess(MonoTime::Now(MonoTime::COARSE)).ToMicroseconds();

  html << Substitute("<tr><td>$0</td><td>$1</td><td>$2 us.</td><td>$3 us.</td><td>$4</td>",
                     EscapeForHtmlToString(scanner.tablet_id()), // $0
                     EscapeForHtmlToString(scanner.id()), // $1
                     time_in_flight_us, time_since_last_access_us, // $2, $3
                     EscapeForHtmlToString(scanner.requestor_string())); // $4


  if (!scanner.IsInitialized()) {
    html << "<td colspan=\"3\">&lt;not yet initialized&gt;</td></tr>";
    return html.str();
  }

  const Schema* projection = &scanner.iter()->schema();

  vector<IteratorStats> stats;
  scanner.GetIteratorStats(&stats);
  CHECK_EQ(stats.size(), projection->num_columns());
  html << Substitute("<td>$0</td>", IteratorStatsToHtml(*projection, stats));
  scoped_refptr<TabletPeer> tablet_peer;
  if (!tserver_->tablet_manager()->LookupTablet(scanner.tablet_id(), &tablet_peer)) {
    html << Substitute("<td colspan=\"2\"><b>Tablet $0 is no longer valid.</b></td></tr>\n",
                       scanner.tablet_id());
  } else {
    string range_pred_str;
    vector<string> other_preds;
    const ScanSpec& spec = scanner.spec();
    if (spec.lower_bound_key() || spec.exclusive_upper_bound_key()) {
      range_pred_str = EncodedKey::RangeToString(spec.lower_bound_key(),
                                                 spec.exclusive_upper_bound_key());
    }
    for (const ColumnRangePredicate& pred : scanner.spec().predicates()) {
      other_preds.push_back(pred.ToString());
    }
    string other_pred_str = JoinStrings(other_preds, "\n");
    html << Substitute("<td>$0</td><td>$1</td></tr>\n",
                       EscapeForHtmlToString(range_pred_str),
                       EscapeForHtmlToString(other_pred_str));
  }
  return html.str();
}

string TabletServerPathHandlers::IteratorStatsToHtml(const Schema& projection,
                                                     const vector<IteratorStats>& stats) const {
  std::stringstream html;
  html << "<table>\n";
  html << "<tr><th>Column</th>"
       << "<th>Blocks read from disk</th>"
       << "<th>Bytes read from disk</th>"
       << "<th>Cells read from disk</th>"
       << "</tr>\n";
  for (size_t idx = 0; idx < stats.size(); idx++) {
    // We use 'title' attributes so that if the user hovers over the value, they get a
    // human-readable tooltip.
    html << Substitute("<tr>"
                       "<td>$0</td>"
                       "<td title=\"$1\">$2</td>"
                       "<td title=\"$3\">$4</td>"
                       "<td title=\"$5\">$6</td>"
                       "</tr>\n",
                       EscapeForHtmlToString(projection.column(idx).name()), // $0
                       HumanReadableInt::ToString(stats[idx].data_blocks_read_from_disk), // $1
                       stats[idx].data_blocks_read_from_disk, // $2
                       HumanReadableNumBytes::ToString(stats[idx].bytes_read_from_disk), // $3
                       stats[idx].bytes_read_from_disk, // $4
                       HumanReadableInt::ToString(stats[idx].cells_read_from_disk), // $5
                       stats[idx].cells_read_from_disk); // $6
  }
  html << "</table>\n";
  return html.str();
}

void TabletServerPathHandlers::HandleDashboardsPage(const Webserver::WebRequest& req,
                                                    std::stringstream* output) {

  *output << "<h3>Dashboards</h3>\n";
  *output << "<table class='table table-striped'>\n";
  *output << "  <tr><th>Dashboard</th><th>Description</th></tr>\n";
  *output << GetDashboardLine("scans", "Scans", "List of scanners that are currently running.");
  *output << GetDashboardLine("transactions", "Transactions", "List of transactions that are "
                                                              "currently running.");
  *output << GetDashboardLine("maintenance-manager", "Maintenance Manager",
                              "List of operations that are currently running and those "
                              "that are registered.");
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
                                                            std::stringstream* output) {
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

} // namespace tserver
} // namespace kudu
