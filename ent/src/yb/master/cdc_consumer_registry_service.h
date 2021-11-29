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

#ifndef ENT_SRC_YB_MASTER_CDC_CONSUMER_REGISTRY_SERVICE_H
#define ENT_SRC_YB_MASTER_CDC_CONSUMER_REGISTRY_SERVICE_H

#include <vector>
#include <unordered_set>

#include "yb/master/master_fwd.h"

#include "yb/util/status_fwd.h"
#include "yb/util/net/net_util.h"

namespace yb {
namespace cdc {

class StreamEntryPB;

} // namespace cdc

namespace master {

class ListTablesResponsePB;
class GetTableLocationsResponsePB;

namespace enterprise {

struct CDCConsumerStreamInfo {
  std::string stream_id;
  std::string consumer_table_id;
  std::string producer_table_id;
};

Status CreateTabletMapping(
    const std::string& producer_table_id,
    const std::string& consumer_table_id,
    const std::string& producer_id,
    const std::string& producer_master_addrs,
    const GetTableLocationsResponsePB& consumer_tablets_resp,
    std::unordered_set<HostPort, HostPortHash>* tserver_addrs,
    cdc::StreamEntryPB* stream_entry);

// After split_tablet_ids.source splits, remove its entry and replace it with its children tablets.
Status UpdateTableMappingOnTabletSplit(
    cdc::StreamEntryPB* stream_entry,
    const SplitTabletIds& split_tablet_ids);

Result<std::vector<CDCConsumerStreamInfo>> TEST_GetConsumerProducerTableMap(
    const std::string& producer_master_addrs,
    const ListTablesResponsePB& resp);

} // namespace enterprise
} // namespace master
} // namespace yb

#endif // ENT_SRC_YB_MASTER_CDC_CONSUMER_REGISTRY_SERVICE_H
