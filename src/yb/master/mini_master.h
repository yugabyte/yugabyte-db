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
#ifndef YB_MASTER_MINI_MASTER_H
#define YB_MASTER_MINI_MASTER_H

#include <string>
#include <vector>

#include "yb/gutil/macros.h"
#include "yb/gutil/port.h"

#include "yb/master/master_fwd.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/util/status_fwd.h"
#include "yb/util/env.h"
#include "yb/util/net/net_fwd.h"

namespace yb {

class FsManager;
class HostPort;

namespace master {

// An in-process Master meant for use in test cases.
//
// TODO: Store the distributed cluster configuration in the object, to avoid
// having multiple Start methods.
class MiniMaster {
 public:
  MiniMaster(Env* env, std::string fs_root, uint16_t rpc_port, uint16_t web_port, int index);
  ~MiniMaster();

  // Start a master running on the loopback interface and
  // an ephemeral port. To determine the address that the server
  // bound to, call MiniMaster::bound_addr()
  CHECKED_STATUS Start(bool TEST_simulate_fs_create_failure = false);

  void set_pass_master_addresses(bool value) {
    pass_master_addresses_ = value;
  }

  CHECKED_STATUS StartDistributedMaster(const std::vector<uint16_t>& peer_ports);

  CHECKED_STATUS WaitForCatalogManagerInit();

  CHECKED_STATUS WaitUntilCatalogManagerIsLeaderAndReadyForTests();

  void Shutdown();

  // Restart the master on the same ports as it was previously bound.
  // Requires that the master is currently started.
  CHECKED_STATUS Restart();

  HostPort bound_rpc_addr() const;
  Endpoint bound_http_addr() const;

  const Master* master() const { return master_.get(); }
  Master* master() { return master_.get(); }

  rpc::Messenger& messenger() const;

  CatalogManagerIf& catalog_manager() const;

  CatalogManager& catalog_manager_impl() const;

  tablet::TabletPeerPtr tablet_peer() const;

  master::SysCatalogTable& sys_catalog() const;

  master::TSManager& ts_manager() const;

  master::FlushManager& flush_manager() const;

  // Return UUID of this mini master.
  std::string permanent_uuid() const;

  std::string bound_rpc_addr_str() const;

  FsManager& fs_manager() const;

 private:
  CHECKED_STATUS StartDistributedMasterOnPorts(uint16_t rpc_port, uint16_t web_port,
                                       const std::vector<uint16_t>& peer_ports);

  CHECKED_STATUS StartOnPorts(uint16_t rpc_port, uint16_t web_port);

  CHECKED_STATUS StartOnPorts(uint16_t rpc_port, uint16_t web_port,
                      MasterOptions* options);

  bool running_;

  ATTRIBUTE_MEMBER_UNUSED Env* const env_;
  const std::string fs_root_;
  const uint16_t rpc_port_, web_port_;

  std::unique_ptr<Master> master_;
  int index_;
  std::unique_ptr<Tunnel> tunnel_;
  bool pass_master_addresses_ = true;
};

} // namespace master
} // namespace yb

#endif /* YB_MASTER_MINI_MASTER_H */
