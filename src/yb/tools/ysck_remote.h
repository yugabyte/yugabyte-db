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

#ifndef YB_TOOLS_KSCK_REMOTE_H
#define YB_TOOLS_KSCK_REMOTE_H

#include <memory>
#include <string>
#include <vector>

#include "yb/master/master.h"
#include "yb/master/master.proxy.h"
#include "yb/rpc/messenger.h"
#include "yb/server/server_base.h"
#include "yb/server/server_base.proxy.h"
#include "yb/tools/ysck.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver_service.proxy.h"
#include "yb/util/net/sockaddr.h"

namespace yb {

class Schema;

namespace tools {

// This implementation connects to a Tablet Server via RPC.
class RemoteYsckTabletServer : public YsckTabletServer {
 public:
  explicit RemoteYsckTabletServer(const std::string& id,
                                  const Sockaddr& address,
                                  const std::shared_ptr<rpc::Messenger>& messenger)
      : YsckTabletServer(id),
        address_(address.ToString()),
        messenger_(messenger),
        generic_proxy_(new server::GenericServiceProxy(messenger, address)),
        ts_proxy_(new tserver::TabletServerServiceProxy(messenger, address)) {
  }

  virtual Status Connect() const OVERRIDE;

  virtual Status CurrentTimestamp(uint64_t* timestamp) const OVERRIDE;

  virtual void RunTabletChecksumScanAsync(
      const std::string& tablet_id,
      const Schema& schema,
      const ChecksumOptions& options,
      const ReportResultCallback& callback) OVERRIDE;


  virtual const std::string& address() const OVERRIDE {
    return address_;
  }

 private:
  const std::string address_;
  const std::shared_ptr<rpc::Messenger> messenger_;
  const std::shared_ptr<server::GenericServiceProxy> generic_proxy_;
  const std::shared_ptr<tserver::TabletServerServiceProxy> ts_proxy_;
};

// This implementation connects to a Master via RPC.
class RemoteYsckMaster : public YsckMaster {
 public:

  static Status Build(const Sockaddr& address, std::shared_ptr<YsckMaster>* master);

  virtual ~RemoteYsckMaster() { }

  virtual Status Connect() const OVERRIDE;

  virtual Status RetrieveTabletServers(TSMap* tablet_servers) OVERRIDE;

  virtual Status RetrieveTablesList(std::vector<std::shared_ptr<YsckTable> >* tables) OVERRIDE;

  virtual Status RetrieveTabletsList(const std::shared_ptr<YsckTable>& table) OVERRIDE;

 private:
  explicit RemoteYsckMaster(
      const Sockaddr& address, const std::shared_ptr<rpc::Messenger>& messenger)
      : messenger_(messenger),
        generic_proxy_(new server::GenericServiceProxy(messenger, address)),
        proxy_(new master::MasterServiceProxy(messenger, address)) {}

  Status GetTableInfo(const std::string& table_name, Schema* schema, int* num_replicas);

  // Used to get a batch of tablets from the master, passing a pointer to the
  // seen last key that will be used as the new start key. The
  // last_partition_key is updated to point at the new last key that came in
  // the batch.
  Status GetTabletsBatch(const std::string& table_name, std::string* last_partition_key,
    std::vector<std::shared_ptr<YsckTablet> >& tablets, bool* more_tablets);

  std::shared_ptr<rpc::Messenger> messenger_;
  const std::shared_ptr<server::GenericServiceProxy> generic_proxy_;
  std::shared_ptr<master::MasterServiceProxy> proxy_;
};

} // namespace tools
} // namespace yb

#endif // YB_TOOLS_KSCK_REMOTE_H
