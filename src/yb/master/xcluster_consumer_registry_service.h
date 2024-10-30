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

#include <map>
#include <string>
#include <google/protobuf/repeated_field.h>

#include "yb/common/entity_ids_types.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/util/status_fwd.h"

namespace yb {
namespace cdc {
class StreamEntryPB;
}  // namespace cdc

namespace client {
class YBClient;
}  // namespace client

namespace master {

class ListTablesResponsePB;
class GetTableLocationsResponsePB;
class TabletLocationsPB;

struct KeyRange {
  std::string start_key;
  std::string end_key;
};

Status PopulateXClusterStreamEntryTabletMapping(
    const TableId& producer_table_id, const TableId& consumer_table_id,
    const std::map<std::string, KeyRange>& consumer_tablet_keys, cdc::StreamEntryPB* stream_entry,
    const google::protobuf::RepeatedPtrField<TabletLocationsPB>& producer_table_locations);

Status UpdateTabletMappingOnConsumerSplit(
    const std::map<std::string, KeyRange>& consumer_tablet_keys,
    const SplitTabletIds& split_tablet_ids, cdc::StreamEntryPB* stream_entry);

Status UpdateTabletMappingOnProducerSplit(
    const std::map<std::string, KeyRange>& consumer_tablet_keys,
    const SplitTabletIds& split_tablet_ids, const std::string& split_key, bool* found_source,
    bool* found_all_split_childs, cdc::StreamEntryPB* stream_entry);

}  // namespace master
}  // namespace yb
