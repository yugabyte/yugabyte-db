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

#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include "yb/common/hybrid_time.h"

#include "yb/consensus/consensus_fwd.h"

#include "yb/gutil/ref_counted.h"

#include "yb/util/status_fwd.h"

namespace yb {

class Counter;
class MetricEntity;

namespace tablet {

// A function that returns the time through which the persistence block map, as seen by this server,
// blocks database_oid's stamped writes and commits, or nullopt if the map has no entry for
// database_oid.  op_term is the leader term the op is being appended in.  ServiceUnavailable means
// this server cannot tell whether the database is blocked (e.g., a TServer without a lease); the
// gate refuses the op with that status plus the persisting-blocked error codes, so the client
// retries it.  Any other error refuses the op with that error unchanged.  Called under the Raft
// lock, so it must be cheap and must not block.
using PersistenceBlockLookupFunc =
    std::function<Result<std::optional<HybridTime>>(int64_t op_term, uint32_t database_oid)>;

// A function that returns whether this server's tablets must give up and not seek leadership
// because the server cannot currently judge stamped writes and commits (e.g., a TServer without a
// lease).  Called on every election start, under the Raft lock, and on every leadership sweep, so
// it must be cheap: it may take only briefly held locks, and none that is held while taking the
// Raft lock.
using ShouldRelinquishLeadershipFunc = std::function<bool()>;

// Decides whether a tablet leader may replicate a YSQL write or commit stamped with its database
// and origination time, given the persistence block map that StopPersisting and StartPersisting
// set.  The map lives in the TServer or master, which the tablet layer cannot reach, so each
// server builds one gate with a lookup into its own copy and gives it to every TabletPeer.
class PersistenceGate {
 public:
  // Without should_relinquish_leadership_func, ShouldRelinquishLeadership always returns false.
  PersistenceGate(
      PersistenceBlockLookupFunc lookup_func,
      const scoped_refptr<MetricEntity>& server_metric_entity,
      ShouldRelinquishLeadershipFunc should_relinquish_leadership_func = nullptr);

  // Returns an error if replicate_msg, which already has its op id and hybrid time, must not be
  // replicated.  Called through ConsensusContext::CheckReplicateAllowed, under the Raft lock.
  Status CheckPersistAllowed(const consensus::LWReplicateMsg& replicate_msg) const;

  // Whether this server's tablets must give up leadership and not seek it.
  bool ShouldRelinquishLeadership() const;

 private:
  const PersistenceBlockLookupFunc lookup_func_;
  const ShouldRelinquishLeadershipFunc should_relinquish_leadership_func_;
  const scoped_refptr<Counter> persistence_violation_rejections_;
};

}  // namespace tablet
}  // namespace yb
