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
// Ysck, a tool to run a YB System Check.

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "yb/client/yb_table_name.h"

#include "yb/common/entity_ids_types.h"
#include "yb/common/schema.h"

#include "yb/gutil/callback_forward.h"

#include "yb/util/status_fwd.h"

namespace yb {
class MonoDelta;
namespace tools {

// Options for checksum scans.
struct ChecksumOptions {
 public:

  ChecksumOptions();

  ChecksumOptions(MonoDelta timeout, int scan_concurrency);

  // The maximum total time to wait for results to come back from all replicas.
  MonoDelta timeout;

  // The maximum number of concurrent checksum scans to run per tablet server.
  int scan_concurrency;
};

// Representation of a tablet replica on a tablet server.
class YsckTabletReplica {
 public:
  YsckTabletReplica(const std::string ts_uuid, const bool is_leader, const bool is_follower)
      : is_leader_(is_leader),
        is_follower_(is_follower),
        ts_uuid_(ts_uuid) {
  }

  const bool& is_leader() const {
    return is_leader_;
  }

  const bool& is_follower() const {
    return is_follower_;
  }

  const std::string& ts_uuid() const {
    return ts_uuid_;
  }

  std::string ToString() const;

 private:
  const bool is_leader_;
  const bool is_follower_;
  const std::string ts_uuid_;
  DISALLOW_COPY_AND_ASSIGN(YsckTabletReplica);
};

// Representation of a tablet belonging to a table. The tablet is composed of replicas.
class YsckTablet {
 public:
  // TODO add start/end keys, stale.
  explicit YsckTablet(std::string id) : id_(std::move(id)) {}

  const std::string& id() const {
    return id_;
  }

  const std::vector<std::shared_ptr<YsckTabletReplica> >& replicas() const {
    return replicas_;
  }

  void set_replicas(const std::vector<std::shared_ptr<YsckTabletReplica> >& replicas) {
    replicas_.assign(replicas.begin(), replicas.end());
  }
 private:
  const std::string id_;
  std::vector<std::shared_ptr<YsckTabletReplica>> replicas_;
  DISALLOW_COPY_AND_ASSIGN(YsckTablet);
};

// Representation of a table. Composed of tablets.
class YsckTable {
 public:
  YsckTable(
      const TableId& id,
      client::YBTableName name,
      const Schema& schema,
      int num_replicas,
      TableType table_type)
      : id_(id),
        name_(std::move(name)),
        schema_(schema),
        num_replicas_(num_replicas),
        table_type_(table_type) {}

  const TableId& id() const {
    return id_;
  }

  const client::YBTableName& name() const {
    return name_;
  }

  const Schema& schema() const {
    return schema_;
  }

  int num_replicas() const {
    return num_replicas_;
  }

  const TableType table_type() const {
    return table_type_;
  }

  void set_tablets(const std::vector<std::shared_ptr<YsckTablet>>& tablets) {
    tablets_.assign(tablets.begin(), tablets.end());
  }

  std::vector<std::shared_ptr<YsckTablet> >& tablets() {
    return tablets_;
  }

  std::string ToString() const;

 private:
  TableId id_;
  const client::YBTableName name_;
  const Schema schema_;
  const int num_replicas_;
  const TableType table_type_;
  std::vector<std::shared_ptr<YsckTablet>> tablets_;
  DISALLOW_COPY_AND_ASSIGN(YsckTable);
};

typedef Callback<void(const Status& status, uint64_t checksum)> ReportResultCallback;

// The following two classes must be extended in order to communicate with their respective
// components. The two main use cases envisioned for this are:
// - To be able to mock a cluster to more easily test the Ysck checks.
// - To be able to communicate with a real YB cluster.

// Class that must be extended to represent a tablet server.
class YsckTabletServer {
 public:
  explicit YsckTabletServer(std::string uuid) : uuid_(std::move(uuid)) {}
  virtual ~YsckTabletServer() { }

  // Connects to the configured Tablet Server.
  virtual Status Connect() const = 0;

  virtual Status CurrentHybridTime(uint64_t* hybrid_time) const = 0;

  // Executes a checksum scan on the associated tablet, and runs the callback
  // with the result. The callback must be threadsafe and non-blocking.
  virtual void RunTabletChecksumScanAsync(
                  const std::string& tablet_id,
                  const Schema& schema,
                  const ChecksumOptions& options,
                  const ReportResultCallback& callback) = 0;

  virtual const std::string& uuid() const {
    return uuid_;
  }

  virtual const std::string& address() const = 0;

 private:
  const std::string uuid_;
  DISALLOW_COPY_AND_ASSIGN(YsckTabletServer);
};

// Class that must be extended to represent a master.
class YsckMaster {
 public:
  // Map of YsckTabletServer objects keyed by tablet server permanent_uuid.
  typedef std::unordered_map<std::string, std::shared_ptr<YsckTabletServer> > TSMap;

  YsckMaster() { }
  virtual ~YsckMaster() { }

  // Connects to the configured Master.
  virtual Status Connect() const = 0;

  // Gets the list of Tablet Servers from the Master and stores it in the passed
  // map, which is keyed on server permanent_uuid.
  // 'tablet_servers' is only modified if this method returns OK.
  virtual Status RetrieveTabletServers(TSMap* tablet_servers) = 0;

  // Gets the list of tables from the Master and stores it in the passed vector.
  // tables is only modified if this method returns OK.
  virtual Status RetrieveTablesList(
      std::vector<std::shared_ptr<YsckTable> >* tables) = 0;

  // Gets the list of tablets for the specified table and stores the list in it.
  // The table's tablet list is only modified if this method returns OK.
  virtual Status RetrieveTabletsList(const std::shared_ptr<YsckTable>& table) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(YsckMaster);
};

// Class used to communicate with the cluster. It bootstraps this by using the provided master.
class YsckCluster {
 public:
  explicit YsckCluster(std::shared_ptr<YsckMaster> master)
      : master_(std::move(master)) {}
  ~YsckCluster();

  // Fetches list of tables, tablets, and tablet servers from the master and
  // populates the full list in cluster_->tables().
  Status FetchTableAndTabletInfo();

  const std::shared_ptr<YsckMaster>& master() {
    return master_;
  }

  const std::unordered_map<std::string,
                           std::shared_ptr<YsckTabletServer> >& tablet_servers() {
    return tablet_servers_;
  }

  const std::vector<std::shared_ptr<YsckTable> >& tables() {
    return tables_;
  }

 private:
  // Gets the list of tablet servers from the Master.
  Status RetrieveTabletServers();

  // Gets the list of tables from the Master.
  Status RetrieveTablesList();

  // Fetch the list of tablets for the given table from the Master.
  Status RetrieveTabletsList(const std::shared_ptr<YsckTable>& table);

  const std::shared_ptr<YsckMaster> master_;
  std::unordered_map<std::string, std::shared_ptr<YsckTabletServer> > tablet_servers_;
  std::vector<std::shared_ptr<YsckTable> > tables_;
  DISALLOW_COPY_AND_ASSIGN(YsckCluster);
};

// Externally facing class to run checks against the provided cluster.
class Ysck {
 public:
  explicit Ysck(std::shared_ptr<YsckCluster> cluster)
      : cluster_(std::move(cluster)) {}
  ~Ysck() {}

  // Verifies that it can connect to the Master.
  Status CheckMasterRunning();

  // Populates all the cluster table and tablet info from the Master.
  Status FetchTableAndTabletInfo();

  // Verifies that it can connect to all the Tablet Servers reported by the master.
  // Must first call FetchTableAndTabletInfo().
  Status CheckTabletServersRunning();

  // Establishes a connection with the specified Tablet Server.
  // Must first call FetchTableAndTabletInfo().
  Status ConnectToTabletServer(const std::shared_ptr<YsckTabletServer>& ts);

  // Verifies that all the tables have contiguous tablets and that each tablet has enough replicas
  // and a leader.
  // Must first call FetchTableAndTabletInfo().
  Status CheckTablesConsistency();

  // Verifies data checksums on all tablets by doing a scan of the database on each replica.
  // If tables is not empty, checks only the named tables.
  // If tablets is not empty, checks only the specified tablets.
  // If both are specified, takes the intersection.
  // If both are empty, all tables and tablets are checked.
  // Must first call FetchTableAndTabletInfo().
  Status ChecksumData(const std::vector<std::string>& tables,
                      const std::vector<std::string>& tablets,
                      const ChecksumOptions& options);

  // Verifies that the assignments reported by the master are the same reported by the
  // Tablet Servers.
  // Must first call FetchTableAndTabletInfo().
  Status CheckAssignments();

 private:
  bool VerifyTable(const std::shared_ptr<YsckTable>& table);
  bool VerifyTableWithTimeout(const std::shared_ptr<YsckTable>& table,
                              const MonoDelta& timeout,
                              const MonoDelta& retry_interval);
  bool VerifyTablet(const std::shared_ptr<YsckTablet>& tablet, size_t table_num_replicas);

  const std::shared_ptr<YsckCluster> cluster_;
  DISALLOW_COPY_AND_ASSIGN(Ysck);
};
} // namespace tools
} // namespace yb
