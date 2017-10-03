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

#include "kudu/tools/ksck.h"

#include <glog/logging.h>
#include <iostream>
#include <unordered_set>

#include "kudu/gutil/map-util.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/gutil/strings/join.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/blocking_queue.h"
#include "kudu/util/locks.h"
#include "kudu/util/monotime.h"

namespace kudu {
namespace tools {

using std::cerr;
using std::cout;
using std::endl;
using std::ostream;
using std::shared_ptr;
using std::string;
using std::unordered_map;
using strings::Substitute;

DEFINE_int32(checksum_timeout_sec, 120,
             "Maximum total seconds to wait for a checksum scan to complete "
             "before timing out.");
DEFINE_int32(checksum_scan_concurrency, 4,
             "Number of concurrent checksum scans to execute per tablet server.");
DEFINE_bool(checksum_snapshot, true, "Should the checksum scanner use a snapshot scan");
DEFINE_uint64(checksum_snapshot_timestamp, ChecksumOptions::kCurrentTimestamp,
              "timestamp to use for snapshot checksum scans, defaults to 0, which "
              "uses the current timestamp of a tablet server involved in the scan");

// Print an informational message to cerr.
static ostream& Info() {
  cerr << "INFO: ";
  return cerr;
}

// Print a warning message to cerr.
static ostream& Warn() {
  cerr << "WARNING: ";
  return cerr;
}

// Print an error message to cerr.
static ostream& Error() {
  cerr << "ERROR: ";
  return cerr;
}

ChecksumOptions::ChecksumOptions()
    : timeout(MonoDelta::FromSeconds(FLAGS_checksum_timeout_sec)),
      scan_concurrency(FLAGS_checksum_scan_concurrency),
      use_snapshot(FLAGS_checksum_snapshot),
      snapshot_timestamp(FLAGS_checksum_snapshot_timestamp) {
}

ChecksumOptions::ChecksumOptions(MonoDelta timeout, int scan_concurrency,
                                 bool use_snapshot, uint64_t snapshot_timestamp)
    : timeout(std::move(timeout)),
      scan_concurrency(scan_concurrency),
      use_snapshot(use_snapshot),
      snapshot_timestamp(snapshot_timestamp) {}

const uint64_t ChecksumOptions::kCurrentTimestamp = 0;

KsckCluster::~KsckCluster() {
}

Status KsckCluster::FetchTableAndTabletInfo() {
  RETURN_NOT_OK(master_->Connect());
  RETURN_NOT_OK(RetrieveTablesList());
  RETURN_NOT_OK(RetrieveTabletServers());
  for (const shared_ptr<KsckTable>& table : tables()) {
    RETURN_NOT_OK(RetrieveTabletsList(table));
  }
  return Status::OK();
}

// Gets the list of tablet servers from the Master.
Status KsckCluster::RetrieveTabletServers() {
  return master_->RetrieveTabletServers(&tablet_servers_);
}

// Gets the list of tables from the Master.
Status KsckCluster::RetrieveTablesList() {
  return master_->RetrieveTablesList(&tables_);
}

Status KsckCluster::RetrieveTabletsList(const shared_ptr<KsckTable>& table) {
  return master_->RetrieveTabletsList(table);
}

Status Ksck::CheckMasterRunning() {
  VLOG(1) << "Connecting to the Master";
  Status s = cluster_->master()->Connect();
  if (s.ok()) {
    Info() << "Connected to the Master" << endl;
  }
  return s;
}

Status Ksck::FetchTableAndTabletInfo() {
  return cluster_->FetchTableAndTabletInfo();
}

Status Ksck::CheckTabletServersRunning() {
  VLOG(1) << "Getting the Tablet Servers list";
  int servers_count = cluster_->tablet_servers().size();
  VLOG(1) << Substitute("List of $0 Tablet Servers retrieved", servers_count);

  if (servers_count == 0) {
    return Status::NotFound("No tablet servers found");
  }

  int bad_servers = 0;
  VLOG(1) << "Connecting to all the Tablet Servers";
  for (const KsckMaster::TSMap::value_type& entry : cluster_->tablet_servers()) {
    Status s = ConnectToTabletServer(entry.second);
    if (!s.ok()) {
      bad_servers++;
    }
  }
  if (bad_servers == 0) {
    Info() << Substitute("Connected to all $0 Tablet Servers", servers_count) << endl;
    return Status::OK();
  } else {
    Warn() << Substitute("Connected to $0 Tablet Servers, $1 weren't reachable",
                         servers_count - bad_servers, bad_servers) << endl;
    return Status::NetworkError("Not all Tablet Servers are reachable");
  }
}

Status Ksck::ConnectToTabletServer(const shared_ptr<KsckTabletServer>& ts) {
  VLOG(1) << "Going to connect to Tablet Server: " << ts->uuid();
  Status s = ts->Connect();
  if (s.ok()) {
    VLOG(1) << "Connected to Tablet Server: " << ts->uuid();
  } else {
    Warn() << Substitute("Unable to connect to Tablet Server $0 because $1",
                         ts->uuid(), s.ToString()) << endl;
  }
  return s;
}

Status Ksck::CheckTablesConsistency() {
  VLOG(1) << "Getting the tables list";
  int tables_count = cluster_->tables().size();
  VLOG(1) << Substitute("List of $0 tables retrieved", tables_count);

  if (tables_count == 0) {
    Info() << "The cluster doesn't have any tables" << endl;
    return Status::OK();
  }

  VLOG(1) << "Verifying each table";
  int bad_tables_count = 0;
  for (const shared_ptr<KsckTable> &table : cluster_->tables()) {
    if (!VerifyTable(table)) {
      bad_tables_count++;
    }
  }
  if (bad_tables_count == 0) {
    Info() << Substitute("The metadata for $0 tables is HEALTHY", tables_count) << endl;
    return Status::OK();
  } else {
    Warn() << Substitute("$0 out of $1 tables are not in a healthy state",
                         bad_tables_count, tables_count) << endl;
    return Status::Corruption(Substitute("$0 tables are bad", bad_tables_count));
  }
}

// Class to act as a collector of scan results.
// Provides thread-safe accessors to update and read a hash table of results.
class ChecksumResultReporter : public RefCountedThreadSafe<ChecksumResultReporter> {
 public:
  typedef std::pair<Status, uint64_t> ResultPair;
  typedef std::unordered_map<std::string, ResultPair> ReplicaResultMap;
  typedef std::unordered_map<std::string, ReplicaResultMap> TabletResultMap;

  // Initialize reporter with the number of replicas being queried.
  explicit ChecksumResultReporter(int num_tablet_replicas)
      : responses_(num_tablet_replicas) {
  }

  // Write an entry to the result map indicating a response from the remote.
  void ReportResult(const std::string& tablet_id,
                    const std::string& replica_uuid,
                    const Status& status,
                    uint64_t checksum) {
    lock_guard<simple_spinlock> guard(&lock_);
    unordered_map<string, ResultPair>& replica_results =
        LookupOrInsert(&checksums_, tablet_id, unordered_map<string, ResultPair>());
    InsertOrDie(&replica_results, replica_uuid, ResultPair(status, checksum));
    responses_.CountDown();
  }

  // Blocks until either the number of results plus errors reported equals
  // num_tablet_replicas (from the constructor), or until the timeout expires,
  // whichever comes first.
  // Returns false if the timeout expired before all responses came in.
  // Otherwise, returns true.
  bool WaitFor(const MonoDelta& timeout) const { return responses_.WaitFor(timeout); }

  // Returns true iff all replicas have reported in.
  bool AllReported() const { return responses_.count() == 0; }

  // Get reported results.
  TabletResultMap checksums() const {
    lock_guard<simple_spinlock> guard(&lock_);
    return checksums_;
  }

 private:
  friend class RefCountedThreadSafe<ChecksumResultReporter>;
  ~ChecksumResultReporter() {}

  // Report either a success or error response.
  void HandleResponse(const std::string& tablet_id, const std::string& replica_uuid,
                      const Status& status, uint64_t checksum);

  CountDownLatch responses_;
  mutable simple_spinlock lock_; // Protects 'checksums_'.
  // checksums_ is an unordered_map of { tablet_id : { replica_uuid : checksum } }.
  TabletResultMap checksums_;
};

// Queue of tablet replicas for an individual tablet server.
typedef shared_ptr<BlockingQueue<std::pair<Schema, std::string> > > TabletQueue;

// A callback function which records the result of a tablet replica's checksum,
// and then checks if the tablet server has any more tablets to checksum. If so,
// a new async checksum scan is started.
void TabletServerChecksumCallback(
    const scoped_refptr<ChecksumResultReporter>& reporter,
    const shared_ptr<KsckTabletServer>& tablet_server,
    const TabletQueue& queue,
    const std::string& tablet_id,
    const ChecksumOptions& options,
    const Status& status,
    uint64_t checksum) {
  reporter->ReportResult(tablet_id, tablet_server->uuid(), status, checksum);

  std::pair<Schema, std::string> table_tablet;
  if (queue->BlockingGet(&table_tablet)) {
    const Schema& table_schema = table_tablet.first;
    const std::string& tablet_id = table_tablet.second;
    ReportResultCallback callback = Bind(&TabletServerChecksumCallback,
                                         reporter,
                                         tablet_server,
                                         queue,
                                         tablet_id,
                                         options);
    tablet_server->RunTabletChecksumScanAsync(tablet_id, table_schema, options, callback);
  }
}

Status Ksck::ChecksumData(const vector<string>& tables,
                          const vector<string>& tablets,
                          const ChecksumOptions& opts) {
  const unordered_set<string> tables_filter(tables.begin(), tables.end());
  const unordered_set<string> tablets_filter(tablets.begin(), tablets.end());

  // Copy options so that local modifications can be made and passed on.
  ChecksumOptions options = opts;

  typedef unordered_map<shared_ptr<KsckTablet>, shared_ptr<KsckTable>> TabletTableMap;
  TabletTableMap tablet_table_map;

  int num_tablet_replicas = 0;
  for (const shared_ptr<KsckTable>& table : cluster_->tables()) {
    VLOG(1) << "Table: " << table->name();
    if (!tables_filter.empty() && !ContainsKey(tables_filter, table->name())) continue;
    for (const shared_ptr<KsckTablet>& tablet : table->tablets()) {
      VLOG(1) << "Tablet: " << tablet->id();
      if (!tablets_filter.empty() && !ContainsKey(tablets_filter, tablet->id())) continue;
      InsertOrDie(&tablet_table_map, tablet, table);
      num_tablet_replicas += tablet->replicas().size();
    }
  }
  if (num_tablet_replicas == 0) {
    string msg = "No tablet replicas found.";
    if (!tables.empty() || !tablets.empty()) {
      msg += " Filter: ";
      if (!tables.empty()) {
        msg += "tables=" + JoinStrings(tables, ",") + ".";
      }
      if (!tablets.empty()) {
        msg += "tablets=" + JoinStrings(tablets, ",") + ".";
      }
    }
    return Status::NotFound(msg);
  }

  // Map of tablet servers to tablet queue.
  typedef unordered_map<shared_ptr<KsckTabletServer>, TabletQueue> TabletServerQueueMap;

  TabletServerQueueMap tablet_server_queues;
  scoped_refptr<ChecksumResultReporter> reporter(new ChecksumResultReporter(num_tablet_replicas));

  // Create a queue of checksum callbacks grouped by the tablet server.
  for (const TabletTableMap::value_type& entry : tablet_table_map) {
    const shared_ptr<KsckTablet>& tablet = entry.first;
    const shared_ptr<KsckTable>& table = entry.second;
    for (const shared_ptr<KsckTabletReplica>& replica : tablet->replicas()) {
      const shared_ptr<KsckTabletServer>& ts =
          FindOrDie(cluster_->tablet_servers(), replica->ts_uuid());

      const TabletQueue& queue =
          LookupOrInsertNewSharedPtr(&tablet_server_queues, ts, num_tablet_replicas);
      CHECK_EQ(QUEUE_SUCCESS, queue->Put(make_pair(table->schema(), tablet->id())));
    }
  }

  if (options.use_snapshot && options.snapshot_timestamp == ChecksumOptions::kCurrentTimestamp) {
    // Set the snapshot timestamp to the current timestamp of an arbitrary tablet server.
    tablet_server_queues.begin()->first->CurrentTimestamp(&options.snapshot_timestamp);
    Info() << "Using snapshot timestamp: " << options.snapshot_timestamp << endl;
  }

  // Kick off checksum scans in parallel. For each tablet server, we start
  // scan_concurrency scans. Each callback then initiates one additional
  // scan when it returns if the queue for that TS is not empty.
  for (const TabletServerQueueMap::value_type& entry : tablet_server_queues) {
    const shared_ptr<KsckTabletServer>& tablet_server = entry.first;
    const TabletQueue& queue = entry.second;
    queue->Shutdown(); // Ensures that BlockingGet() will not block.
    for (int i = 0; i < options.scan_concurrency; i++) {
      std::pair<Schema, std::string> table_tablet;
      if (queue->BlockingGet(&table_tablet)) {
        const Schema& table_schema = table_tablet.first;
        const std::string& tablet_id = table_tablet.second;
        ReportResultCallback callback = Bind(&TabletServerChecksumCallback,
                                             reporter,
                                             tablet_server,
                                             queue,
                                             tablet_id,
                                             options);
        tablet_server->RunTabletChecksumScanAsync(tablet_id, table_schema, options, callback);
      }
    }
  }

  bool timed_out = false;
  if (!reporter->WaitFor(options.timeout)) {
    timed_out = true;
  }
  ChecksumResultReporter::TabletResultMap checksums = reporter->checksums();

  int num_errors = 0;
  int num_mismatches = 0;
  int num_results = 0;
  for (const shared_ptr<KsckTable>& table : cluster_->tables()) {
    bool printed_table_name = false;
    for (const shared_ptr<KsckTablet>& tablet : table->tablets()) {
      if (ContainsKey(checksums, tablet->id())) {
        if (!printed_table_name) {
          printed_table_name = true;
          cout << "-----------------------" << endl;
          cout << table->name() << endl;
          cout << "-----------------------" << endl;
        }
        bool seen_first_replica = false;
        uint64_t first_checksum = 0;

        for (const ChecksumResultReporter::ReplicaResultMap::value_type& r :
                      FindOrDie(checksums, tablet->id())) {
          const string& replica_uuid = r.first;

          shared_ptr<KsckTabletServer> ts = FindOrDie(cluster_->tablet_servers(), replica_uuid);
          const ChecksumResultReporter::ResultPair& result = r.second;
          const Status& status = result.first;
          uint64_t checksum = result.second;
          string status_str = (status.ok()) ? Substitute("Checksum: $0", checksum)
                                            : Substitute("Error: $0", status.ToString());
          cout << Substitute("T $0 P $1 ($2): $3", tablet->id(), ts->uuid(), ts->address(),
                                                   status_str) << endl;
          if (!status.ok()) {
            num_errors++;
          } else if (!seen_first_replica) {
            seen_first_replica = true;
            first_checksum = checksum;
          } else if (checksum != first_checksum) {
            num_mismatches++;
            Error() << ">> Mismatch found in table " << table->name()
                    << " tablet " << tablet->id() << endl;
          }
          num_results++;
        }
      }
    }
    if (printed_table_name) cout << endl;
  }
  if (num_results != num_tablet_replicas) {
    CHECK(timed_out) << Substitute("Unexpected error: only got $0 out of $1 replica results",
                                   num_results, num_tablet_replicas);
    return Status::TimedOut(Substitute("Checksum scan did not complete within the timeout of $0: "
                                       "Received results for $1 out of $2 expected replicas",
                                       options.timeout.ToString(), num_results,
                                       num_tablet_replicas));
  }
  if (num_mismatches != 0) {
    return Status::Corruption(Substitute("$0 checksum mismatches were detected", num_mismatches));
  }
  if (num_errors != 0) {
    return Status::Aborted(Substitute("$0 errors were detected", num_errors));
  }

  return Status::OK();
}

bool Ksck::VerifyTable(const shared_ptr<KsckTable>& table) {
  bool good_table = true;
  vector<shared_ptr<KsckTablet> > tablets = table->tablets();
  int tablets_count = tablets.size();
  if (tablets_count == 0) {
    Warn() << Substitute("Table $0 has 0 tablets", table->name()) << endl;
    return false;
  }
  int table_num_replicas = table->num_replicas();
  VLOG(1) << Substitute("Verifying $0 tablets for table $1 configured with num_replicas = $2",
                        tablets_count, table->name(), table_num_replicas);
  int bad_tablets_count = 0;
  // TODO check if the tablets are contiguous and in order.
  for (const shared_ptr<KsckTablet> &tablet : tablets) {
    if (!VerifyTablet(tablet, table_num_replicas)) {
      bad_tablets_count++;
    }
  }
  if (bad_tablets_count == 0) {
    Info() << Substitute("Table $0 is HEALTHY", table->name()) << endl;
  } else {
    Warn() << Substitute("Table $0 has $1 bad tablets", table->name(), bad_tablets_count) << endl;
    good_table = false;
  }
  return good_table;
}

bool Ksck::VerifyTablet(const shared_ptr<KsckTablet>& tablet, int table_num_replicas) {
  vector<shared_ptr<KsckTabletReplica> > replicas = tablet->replicas();
  bool good_tablet = true;
  if (replicas.size() != table_num_replicas) {
    Warn() << Substitute("Tablet $0 has $1 instead of $2 replicas",
                         tablet->id(), replicas.size(), table_num_replicas) << endl;
    // We only fail the "goodness" check if the tablet is under-replicated.
    if (replicas.size() < table_num_replicas) {
      good_tablet = false;
    }
  }
  int leaders_count = 0;
  int followers_count = 0;
  for (const shared_ptr<KsckTabletReplica> replica : replicas) {
    if (replica->is_leader()) {
      VLOG(1) << Substitute("Replica at $0 is a LEADER", replica->ts_uuid());
      leaders_count++;
    } else if (replica->is_follower()) {
      VLOG(1) << Substitute("Replica at $0 is a FOLLOWER", replica->ts_uuid());
      followers_count++;
    }
  }
  if (leaders_count == 0) {
    Warn() << Substitute("Tablet $0 doesn't have a leader", tablet->id()) << endl;
    good_tablet = false;
  }
  VLOG(1) << Substitute("Tablet $0 has $1 leader and $2 followers",
                        tablet->id(), leaders_count, followers_count);
  return good_tablet;
}

Status Ksck::CheckAssignments() {
  // TODO
  return Status::NotSupported("CheckAssignments hasn't been implemented");
}

} // namespace tools
} // namespace kudu
