// Copyright (c) YugaByte, Inc.
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include "yb/tablet/operations/change_auto_flags_config_operation.h"

#include "yb/client/auto_flags_manager.h"
#include "yb/consensus/consensus.messages.h"

#include "yb/tablet/tablet.h"

#include "yb/util/logging.h"

namespace yb {
namespace tablet {

template <>
void RequestTraits<LWAutoFlagsConfigPB>::SetAllocatedRequest(
    consensus::LWReplicateMsg* replicate, LWAutoFlagsConfigPB* request) {
  replicate->ref_auto_flags_config(request);
}

template <>
LWAutoFlagsConfigPB* RequestTraits<LWAutoFlagsConfigPB>::MutableRequest(
    consensus::LWReplicateMsg* replicate) {
  return replicate->mutable_auto_flags_config();
}

Status ChangeAutoFlagsConfigOperation::Prepare(IsLeaderSide is_leader_side) {
  VLOG_WITH_PREFIX_AND_FUNC(2);
  return Status::OK();
}

Status ChangeAutoFlagsConfigOperation::DoAborted(const Status& status) {
  VLOG_WITH_PREFIX_AND_FUNC(2);
  return status;
}

Status ChangeAutoFlagsConfigOperation::Apply() {
  VLOG_WITH_PREFIX_AND_FUNC(2) << "Started";

  auto tablet = VERIFY_RESULT(tablet_safe());

  // Store the new AutoFlags config to disk and then applies it. Error Status is returned only for
  // critical failures like IO issues and invalid flags (indicating we are running an unsupported
  // code version). Execution must stop immediately to protect data correctness, so we return the
  // Status directly instead of setting complete_status.
  RETURN_NOT_OK(tablet->ProcessAutoFlagsConfigOperation(request()->ToGoogleProtobuf()));

  VLOG_WITH_PREFIX_AND_FUNC(2) << "Completed";

  return Status::OK();
}

Status ChangeAutoFlagsConfigOperation::DoReplicated(int64_t leader_term, Status* complete_status) {
  return Apply();
}

}  // namespace tablet

}  // namespace yb
