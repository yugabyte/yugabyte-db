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

#include "yb/common/entity_ids_types.h"

#include "yb/consensus/consensus_fwd.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/util/status_fwd.h"

namespace yb {

namespace tserver {

// Pure virtual interface that provides an abstraction for something that
// contains and manages TabletPeers. This interface is implemented on both
// tablet servers and master servers.
// TODO: Rename this interface.
class TabletPeerLookupIf {
 public:
  virtual ~TabletPeerLookupIf() {}

  virtual Result<tablet::TabletPeerPtr> GetServingTablet(const TabletId& tablet_id) const = 0;
  virtual Result<tablet::TabletPeerPtr> GetServingTablet(const Slice& tablet_id) const = 0;

  virtual const NodeInstancePB& NodeInstance() const = 0;

  virtual Status GetRegistration(ServerRegistrationPB* reg) const = 0;

  virtual Status StartRemoteBootstrap(const consensus::StartRemoteBootstrapRequestPB& req) = 0;
};

} // namespace tserver
} // namespace yb
