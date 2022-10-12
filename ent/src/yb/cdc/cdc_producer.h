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

#ifndef ENT_SRC_YB_CDC_CDC_PRODUCER_H
#define ENT_SRC_YB_CDC_CDC_PRODUCER_H

#include <memory>
#include <string>

#include <boost/functional/hash.hpp>
#include <boost/unordered_map.hpp>

#include "yb/cdc/cdc_service.service.h"
#include "yb/client/client_fwd.h"
#include "yb/common/common_fwd.h"
#include "yb/common/transaction.h"
#include "yb/consensus/consensus_fwd.h"
#include "yb/docdb/docdb.pb.h"
#include "yb/tablet/tablet_fwd.h"
#include "yb/util/monotime.h"
#include "yb/util/opid.h"

namespace yb {

class MemTracker;

namespace cdc {

using EnumOidLabelMap = std::unordered_map<std::uint32_t, std::string>;
using EnumLabelCache = std::unordered_map<NamespaceName, EnumOidLabelMap>;

struct StreamMetadata {
  NamespaceId ns_id;
  std::vector<TableId> table_ids;
  CDCRecordType record_type;
  CDCRecordFormat record_format;
  CDCRequestSource source_type;
  CDCCheckpointType checkpoint_type;

  StreamMetadata() = default;

  StreamMetadata(NamespaceId ns_id,
                 std::vector<TableId> table_ids,
                 CDCRecordType record_type,
                 CDCRecordFormat record_format,
                 CDCRequestSource source_type,
                 CDCCheckpointType checkpoint_type)
      : ns_id(std::move(ns_id)),
        table_ids((std::move(table_ids))),
        record_type(record_type),
        record_format(record_format),
        source_type(source_type),
        checkpoint_type(checkpoint_type) {
  }
};

Status GetChangesForCDCSDK(
    const CDCStreamId& stream_id,
    const TableId& tablet_id,
    const CDCSDKCheckpointPB& op_id,
    const StreamMetadata& record,
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    const std::shared_ptr<MemTracker>& mem_tracker,
    const EnumOidLabelMap& enum_oid_label_map,
    client::YBClient* client,
    consensus::ReplicateMsgsHolder* msgs_holder,
    GetChangesResponsePB* resp,
    std::string* commit_timestamp,
    std::shared_ptr<Schema>* cached_schema,
    uint32_t* cached_schema_version,
    OpId* last_streamed_op_id,
    int64_t* last_readable_opid_index = nullptr,
    const CoarseTimePoint deadline = CoarseTimePoint::max());

using UpdateOnSplitOpFunc = std::function<Status(std::shared_ptr<yb::consensus::ReplicateMsg>)>;

Status GetChangesForXCluster(const std::string& stream_id,
                                     const std::string& tablet_id,
                                     const OpId& op_id,
                                     const StreamMetadata& record,
                                     const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
                                     const client::YBSessionPtr& session,
                                     UpdateOnSplitOpFunc update_on_split_op_func,
                                     const std::shared_ptr<MemTracker>& mem_tracker,
                                     consensus::ReplicateMsgsHolder* msgs_holder,
                                     GetChangesResponsePB* resp,
                                     int64_t* last_readable_opid_index = nullptr,
                                     const CoarseTimePoint deadline = CoarseTimePoint::max());
}  // namespace cdc
}  // namespace yb

#endif /* ENT_SRC_YB_CDC_CDC_PRODUCER_H */
