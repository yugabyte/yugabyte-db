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

#include "yb/tablet/persistence_gate.h"

#include "yb/common/common_flags.h"
#include "yb/common/pgsql_error.h"
#include "yb/common/transaction.pb.h"

#include "yb/consensus/consensus.messages.h"

#include "yb/tserver/tserver_error.h"

#include "yb/util/metrics.h"
#include "yb/util/status_format.h"

// Cases where we refuse a write or commit because we cannot tell whether persisting is blocked for
// its database (i.e., we lack an xCluster-guarded info lease) do not cause this counter to be
// bumped.  The xcluster_guarded_info_lease_held gauge shows when a TServer lacks that lease.
METRIC_DEFINE_counter(
    server, persistence_violation_rejections, "Persistence Violation Rejections",
    yb::MetricUnit::kRequests,
    "Number of YSQL writes and commits refused because xCluster had stopped persisting their "
    "database (see StopPersisting and StartPersisting).");

namespace yb::tablet {

namespace {

// This makes Postgres report the database as read-only.
Status AddPersistingBlockedErrorCodes(const Status& status) {
  return status
      .CloneAndAddErrorCode(
          tserver::TabletServerError(tserver::TabletServerErrorPB::PERSISTING_BLOCKED))
      .CloneAndAddErrorCode(PgsqlError(YBPgErrorCode::YB_PG_READ_ONLY_SQL_TRANSACTION));
}

// The stamp of a write or commit, or nullptr if replicate_msg is neither or is unstamped.  Any
// other transaction status update must pass: refusing one would keep a transaction from aborting
// or applying.
const LWOriginationInfoPB* GetOriginationInfo(const consensus::LWReplicateMsg& replicate_msg) {
  switch (replicate_msg.op_type()) {
    case consensus::OperationType::WRITE_OP:
      if (replicate_msg.write().has_origination_info()) {
        return &replicate_msg.write().origination_info();
      }
      return nullptr;
    case consensus::OperationType::UPDATE_TRANSACTION_OP: {
      const auto& state = replicate_msg.transaction_state();
      // Sealing is off by default and appears to have never been used; a sealed transaction commits
      // once its writes land, so treat it like a commit.
      if ((state.status() == TransactionStatus::COMMITTED ||
           state.status() == TransactionStatus::SEALED) &&
          state.has_origination_info()) {
        return &state.origination_info();
      }
      return nullptr;
    }
    default:
      return nullptr;
  }
}

}  // namespace

PersistenceGate::PersistenceGate(
    PersistenceBlockLookupFunc lookup_func, const scoped_refptr<MetricEntity>& server_metric_entity,
    ShouldRelinquishLeadershipFunc should_relinquish_leadership_func)
    : lookup_func_(std::move(lookup_func)),
      should_relinquish_leadership_func_(std::move(should_relinquish_leadership_func)),
      persistence_violation_rejections_(
          METRIC_persistence_violation_rejections.Instantiate(server_metric_entity)) {}

bool PersistenceGate::ShouldRelinquishLeadership() const {
  return should_relinquish_leadership_func_ && should_relinquish_leadership_func_();
}

Status PersistenceGate::CheckPersistAllowed(const consensus::LWReplicateMsg& replicate_msg) const {
  if (!IsPersistenceGateEnforced()) {
    return Status::OK();
  }
  const auto* origination_info = GetOriginationInfo(replicate_msg);
  if (!origination_info) {
    return Status::OK();
  }
  const auto database_oid = origination_info->database_oid();
  const auto lookup_result = lookup_func_(replicate_msg.id().term(), database_oid);
  if (!lookup_result.ok()) {
    // ServiceUnavailable means the TServer is missing a lease and thus cannot tell whether the
    // database is blocked, so the op is refused as possibly blocked and carries the
    // persisting-blocked codes, which rate-limit the logging of each refusal.  The client retries
    // ServiceUnavailable until the TServer regains its lease or leadership moves; if its retries
    // run out first, Postgres reports a timeout-style error, not these codes, since the retries
    // replace the status.  Other errors -- e.g., master's LeaderNotReadyToServe while its
    // catalog is not loaded for this term -- are unrelated to blocking and pass through
    // unchanged.  Neither is counted as a persistence violation refusal.
    if (lookup_result.status().IsServiceUnavailable()) {
      return AddPersistingBlockedErrorCodes(lookup_result.status());
    }
    return lookup_result.status();
  }

  const auto& blocked_through = *lookup_result;
  // Inclusive: the entry is the last blocked origination time.
  if (!blocked_through || HybridTime(origination_info->origination_ht()) > *blocked_through) {
    return Status::OK();
  }

  persistence_violation_rejections_->Increment();
  // This error is final: the client does not retry Expired, and, unlike TryAgain, it does not
  // trigger Postgres's transparent restart.
  return AddPersistingBlockedErrorCodes(STATUS_FORMAT(
      Expired,
      "Writes to the database with OID $0 are temporarily disabled by an xCluster operation",
      database_oid));
}

}  // namespace yb::tablet
