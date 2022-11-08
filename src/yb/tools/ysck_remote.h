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

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "yb/rpc/rpc_fwd.h"

#include "yb/server/server_base.proxy.h"

#include "yb/tools/ysck.h"

#include "yb/tserver/tablet_server.h"

namespace yb {

class Schema;

namespace tools {

// This implementation connects to a Tablet Server via RPC.
class RemoteYsckTabletServer : public YsckTabletServer {
 public:
  explicit RemoteYsckTabletServer(const std::string& id,
                                  const HostPort& address,
                                  rpc::ProxyCache* proxy_cache);

  Status Connect() const override;

  Status CurrentHybridTime(uint64_t* hybrid_time) const override;

  void RunTabletChecksumScanAsync(
      const std::string& tablet_id,
      const Schema& schema,
      const ChecksumOptions& options,
      const ReportResultCallback& callback) override;


  const std::string& address() const override {
    return address_;
  }

 private:
  const std::string address_;
  const std::shared_ptr<server::GenericServiceProxy> generic_proxy_;
  const std::shared_ptr<tserver::TabletServerServiceProxy> ts_proxy_;
};

// This implementation connects to a Master via RPC.
class RemoteYsckMaster : public YsckMaster {
 public:
  static Status Build(const HostPort& address, std::shared_ptr<YsckMaster>* master);

  virtual ~RemoteYsckMaster();

  virtual Status Connect() const override;

  virtual Status RetrieveTabletServers(TSMap* tablet_servers) override;

  virtual Status RetrieveTablesList(
      std::vector<std::shared_ptr<YsckTable> >* tables) override;

  virtual Status RetrieveTabletsList(const std::shared_ptr<YsckTable>& table) override;

 private:
  explicit RemoteYsckMaster(
      const HostPort& address, std::unique_ptr<rpc::Messenger>&& messenger);

  Status GetTableInfo(
      const TableId& table_id, Schema* schema, int* num_replicas, bool* is_pg_table);

  // Used to get a batch of tablets from the master, passing a pointer to the
  // seen last key that will be used as the new start key. The
  // last_partition_key is updated to point at the new last key that came in
  // the batch.
  Status GetTabletsBatch(
      const TableId& table_id,
      const client::YBTableName& table_name,
      std::string* last_partition_key, std::vector<std::shared_ptr<YsckTablet> >* tablets,
      bool* more_tablets);

  std::unique_ptr<rpc::Messenger> messenger_;
  std::unique_ptr<rpc::ProxyCache> proxy_cache_;
  const std::shared_ptr<server::GenericServiceProxy> generic_proxy_;
  std::shared_ptr<master::MasterClientProxy> client_proxy_;
  std::shared_ptr<master::MasterClusterProxy> cluster_proxy_;
  std::shared_ptr<master::MasterDdlProxy> ddl_proxy_;
};

} // namespace tools
} // namespace yb
