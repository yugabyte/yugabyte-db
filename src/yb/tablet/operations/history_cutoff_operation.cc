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

#include "yb/tablet/operations/history_cutoff_operation.h"

#include "yb/consensus/consensus_round.h"

#include "yb/docdb/consensus_frontier.h"

#include "yb/rocksdb/db.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_retention_policy.h"

#include "yb/util/logging.h"

namespace yb {
namespace tablet {

template <>
void RequestTraits<consensus::LWHistoryCutoffPB>::SetAllocatedRequest(
    consensus::LWReplicateMsg* replicate, consensus::LWHistoryCutoffPB* request) {
  replicate->ref_history_cutoff(request);
}

template <>
consensus::LWHistoryCutoffPB* RequestTraits<consensus::LWHistoryCutoffPB>::MutableRequest(
    consensus::LWReplicateMsg* replicate) {
  return replicate->mutable_history_cutoff();
}

Status HistoryCutoffOperation::Apply(int64_t leader_term) {
  auto primary_cutoff = request()->has_primary_cutoff_ht() ?
      HybridTime(request()->primary_cutoff_ht()) : HybridTime();
  auto cotables_cutoff = request()->has_cotables_cutoff_ht() ?
      HybridTime(request()->cotables_cutoff_ht()) : HybridTime();
  docdb::HistoryCutoff history_cutoff(
      { cotables_cutoff, primary_cutoff });

  VLOG_WITH_PREFIX(2) << "History cutoff replicated " << op_id() << ": " << history_cutoff;

  auto tablet = VERIFY_RESULT(tablet_safe());
  history_cutoff = tablet->RetentionPolicy()->UpdateCommittedHistoryCutoff(history_cutoff);
  auto regular_db = tablet->regular_db();
  if (regular_db) {
    rocksdb::WriteBatch batch;
    docdb::ConsensusFrontiers frontiers;
    frontiers.Largest().set_history_cutoff_information(history_cutoff);
    batch.SetFrontiers(&frontiers);
    rocksdb::WriteOptions options;
    RETURN_NOT_OK(regular_db->Write(options, &batch));
  }
  return Status::OK();
}

Status HistoryCutoffOperation::Prepare(IsLeaderSide is_leader_side) {
  VLOG_WITH_PREFIX(2) << "Prepare";
  return Status::OK();
}

Status HistoryCutoffOperation::DoReplicated(int64_t leader_term, Status* complete_status) {
  VLOG_WITH_PREFIX(2) << "Replicated";

  return Apply(leader_term);
}

Status HistoryCutoffOperation::DoAborted(const Status& status) {
  return status;
}

} // namespace tablet
} // namespace yb
