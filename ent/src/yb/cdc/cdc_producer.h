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
#include "yb/common/transaction.h"
#include "yb/consensus/consensus_fwd.h"
#include "yb/docdb/docdb.pb.h"
#include "yb/tablet/tablet_fwd.h"
#include "yb/util/monotime.h"
#include "yb/util/opid.h"

namespace yb {

class MemTracker;

namespace cdc {

struct StreamMetadata {
  TableId table_id;
  CDCRecordType record_type;
  CDCRecordFormat record_format;

  StreamMetadata() = default;

  StreamMetadata(TableId table_id, CDCRecordType record_type, CDCRecordFormat record_format)
      : table_id(std::move(table_id)), record_type(record_type), record_format(record_format) {
  }
};

CHECKED_STATUS GetChanges(const std::string& stream_id,
                          const std::string& tablet_id,
                          const OpId& op_id,
                          const StreamMetadata& record,
                          const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
                          const std::shared_ptr<MemTracker>& mem_tracker,
                          consensus::ReplicateMsgsHolder* msgs_holder,
                          GetChangesResponsePB* resp,
                          int64_t* last_readable_opid_index = nullptr,
                          const CoarseTimePoint deadline = CoarseTimePoint::max());

}  // namespace cdc
}  // namespace yb

#endif /* ENT_SRC_YB_CDC_CDC_PRODUCER_H */
