// Copyright (c) YugaByte, Inc.
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

#include "yb/master/master_fwd.h"
#include "yb/master/master_tserver.h"

#include "yb/tserver/tablet_service.h"

namespace yb {
namespace master {

// A subset of the TabletService supported by the Master to query specific tables.
class MasterTabletServiceImpl : public tserver::TabletServiceImpl {
 public:
  MasterTabletServiceImpl(MasterTabletServer* server, Master* master);

  void Write(const tserver::WriteRequestPB* req,
             tserver::WriteResponsePB* resp,
             rpc::RpcContext context) override;

  void ListTablets(const tserver::ListTabletsRequestPB* req,
                   tserver::ListTabletsResponsePB* resp,
                   rpc::RpcContext context) override;

  void ListTabletsForTabletServer(const tserver::ListTabletsForTabletServerRequestPB* req,
                                  tserver::ListTabletsForTabletServerResponsePB* resp,
                                  rpc::RpcContext context) override;

  void GetLogLocation(const tserver::GetLogLocationRequestPB* req,
                      tserver::GetLogLocationResponsePB* resp,
                      rpc::RpcContext context) override;

  void Checksum(const tserver::ChecksumRequestPB* req,
                tserver::ChecksumResponsePB* resp,
                rpc::RpcContext context) override;

  void IsTabletServerReady(const tserver::IsTabletServerReadyRequestPB* req,
                           tserver::IsTabletServerReadyResponsePB* resp,
                           rpc::RpcContext context) override;

 private:
  Result<std::shared_ptr<tablet::AbstractTablet>> GetTabletForRead(
    const TabletId& tablet_id, tablet::TabletPeerPtr tablet_peer,
    YBConsistencyLevel consistency_level, tserver::AllowSplitTablet allow_split_tablet,
    tserver::ReadResponsePB* resp) override;

  Master *const master_;
  DISALLOW_COPY_AND_ASSIGN(MasterTabletServiceImpl);
};

} // namespace master
} // namespace yb
