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
#ifndef YB_TSERVER_TABLET_PEER_LOOKUP_H_
#define YB_TSERVER_TABLET_PEER_LOOKUP_H_

#include <memory>

#include "yb/util/status_fwd.h"

namespace yb {

class HostPort;
class NodeInstancePB;
class ServerRegistrationPB;

namespace consensus {
class StartRemoteBootstrapRequestPB;
} // namespace consensus

namespace tablet {
class TabletPeer;
} // namespace tablet

namespace tserver {

// Pure virtual interface that provides an abstraction for something that
// contains and manages TabletPeers. This interface is implemented on both
// tablet servers and master servers.
// TODO: Rename this interface.
class TabletPeerLookupIf {
 public:
  virtual ~TabletPeerLookupIf() {}

  virtual CHECKED_STATUS GetTabletPeer(
      const std::string& tablet_id,
      std::shared_ptr<tablet::TabletPeer>* tablet_peer) const = 0;

  virtual const NodeInstancePB& NodeInstance() const = 0;

  virtual CHECKED_STATUS GetRegistration(ServerRegistrationPB* reg) const = 0;

  virtual CHECKED_STATUS StartRemoteBootstrap(
      const consensus::StartRemoteBootstrapRequestPB& req) = 0;
};

} // namespace tserver
} // namespace yb

#endif // YB_TSERVER_TABLET_PEER_LOOKUP_H_
