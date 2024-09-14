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

#include "yb/tablet/operations/truncate_operation.h"

#include "yb/util/logging.h"

#include "yb/consensus/consensus_round.h"
#include "yb/consensus/consensus.messages.h"

#include "yb/tablet/tablet.h"

#include "yb/util/trace.h"

DEFINE_test_flag(bool, skip_applying_truncate, false,
                 "If true, the test will skip applying tablet truncate operation."
                 "Note that other operations will still be applied.");

namespace yb {
namespace tablet {

template <>
void RequestTraits<LWTruncatePB>::SetAllocatedRequest(
    consensus::LWReplicateMsg* replicate, LWTruncatePB* request) {
  replicate->ref_truncate(request);
}

template <>
LWTruncatePB* RequestTraits<LWTruncatePB>::MutableRequest(consensus::LWReplicateMsg* replicate) {
  return replicate->mutable_truncate();
}

Status TruncateOperation::DoAborted(const Status& status) {
  return status;
}

Status TruncateOperation::DoReplicated(int64_t leader_term, Status* complete_status) {
  TRACE("APPLY TRUNCATE: started");

  if (FLAGS_TEST_skip_applying_truncate) {
    TRACE("APPLY TRUNCATE: skipped");
    return Status::OK();
  }

  RETURN_NOT_OK(VERIFY_RESULT(tablet_safe())->Truncate(this));

  TRACE("APPLY TRUNCATE: finished");

  return Status::OK();
}

}  // namespace tablet
}  // namespace yb
