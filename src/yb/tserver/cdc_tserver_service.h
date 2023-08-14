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

#include "yb/cdc/cdc_service.h"
#include "yb/tserver/cdc_tserver_server.h"

#define EMPTY_IMPL(methods) BOOST_PP_SEQ_FOR_EACH(EMPTY_IMPL_HELPER, , methods)
#define EMPTY_IMPL_HELPER(r, x, method_name) \
  void method_name( \
      const BOOST_PP_CAT(yb::cdc::method_name, RequestPB) * req, \
      BOOST_PP_CAT(yb::cdc::method_name, ResponsePB) * resp, \
      rpc::RpcContext rpc) override {}

namespace yb {
namespace cdcserver {

// Service that exposes the required RPCs from CDC Service to a new port
// The service is registered in CDCTServerServer (in cdc_tserver_server.cc)
class CDCTServerServiceImpl : public yb::cdc::CDCServiceImpl {
 public:
  CDCTServerServiceImpl(
    std::unique_ptr<cdc::CDCServiceContext> context,
    const scoped_refptr<MetricEntity>& metric_entity_server,
    MetricRegistry* metric_registry)
    : yb::cdc::CDCServiceImpl(std::move(context), metric_entity_server, metric_registry) {}

  // We only require to expose the following RPCs from CDC Service to the new port.
  // 1. CreateCDCStream
  // 2. GetChanges
  // 3. GetCheckpoint
  // 4. SetCDCCheckpoint
  // 5. GetTabletListToPollForCDC

  // The rest are provided empty implementation (need we return error?)

  EMPTY_IMPL(
    (DeleteCDCStream)
    (ListTablets)
    (UpdateCdcReplicatedIndex)
    (BootstrapProducer)
    (GetCDCDBStreamInfo)
    (IsBootstrapRequired)
    (CheckReplicationDrain)
  );
};
}  // namespace cdcserver
}  // namespace yb
