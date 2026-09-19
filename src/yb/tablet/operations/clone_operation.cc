//
// Copyright (c) YugabyteDB, Inc.
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
//

#include "yb/tablet/operations/clone_operation.h"

#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus.messages.h"
#include "yb/consensus/consensus_error.h"
#include "yb/consensus/consensus_round.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"

#include "yb/util/logging.h"
#include "yb/util/status_format.h"

using namespace std::literals;

namespace yb {
namespace tablet {

template <>
void RequestTraits<LWCloneTabletRequestPB>::SetAllocatedRequest(
    consensus::LWReplicateMsg* replicate, LWCloneTabletRequestPB* request) {
  replicate->ref_clone_tablet(request);
}

template <>
LWCloneTabletRequestPB* RequestTraits<LWCloneTabletRequestPB>::MutableRequest(
    consensus::LWReplicateMsg* replicate) {
  return replicate->mutable_clone_tablet();
}

Status CloneOperation::Prepare(IsLeaderSide is_leader_side) {
  VLOG_WITH_PREFIX(2) << "Prepare";
  return Status::OK();
}

Status CloneOperation::ValidateLeaderOpId(const OpId& op_id) const {
  // Cloning is rejected while an index-backfill write-ID ordering generation is active. The
  // clone's data comes from hard-linked snapshot files containing generation-scoped marked
  // write IDs, but its metadata is built fresh (RaftGroupMetadata::CreateNew, not a superblock
  // copy), so the clone would carry marked versions with no generation record describing them
  // and no backfill job tracking their lifecycle. Backfill is expected to be short-lived
  // relative to clone scheduling, so callers retry after the generation is released.
  const auto generation =
      VERIFY_RESULT(tablet_safe())->metadata()->index_backfill_ordering_generation();
  SCHECK(
      !generation.active, IllegalState,
      "Tablet cloning is fenced while an index-backfill write-ID ordering generation is "
      "active");
  return Status::OK();
}

Status CloneOperation::DoAborted(const Status& status) {
  VLOG_WITH_PREFIX(2) << "DoAborted";
  return status;
}

Status CloneOperation::DoReplicated(int64_t leader_term, Status* complete_status) {
  VLOG_WITH_PREFIX(2) << "Apply";
  return tablet_splitter().ApplyCloneTablet(
          this, /* raft_log = */ nullptr, /* committed_raft_config = */ std::nullopt);
}

}  // namespace tablet
}  // namespace yb
