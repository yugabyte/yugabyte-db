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

#include "yb/common/common_net.pb.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/rpc/rpc_context.h"
#include "yb/rpc/thread_pool.h"

#include "yb/server/clock.h"

#include "yb/tablet/abstract_tablet.h"

#include "yb/tserver/tserver_fwd.h"
#include "yb/tserver/tserver.fwd.h"

namespace yb {
namespace tserver {

// Actually it would be better to rely on TabletServerIf::tablet_peer_lookup, but master
// has pretty different logic in obtaining read tablet.
// Moreover in master we have several places to get tablet, but all of them has different logic.
// So it is good example of bad design that was not refactored in a proper time.
// And now it is non trivial task to merge all such places into a single one.
class ReadTabletProvider {
 public:
  virtual Result<std::shared_ptr<tablet::AbstractTablet>> GetTabletForRead(
      TabletIdView tablet_id, tablet::TabletPeerPtr tablet_peer,
      YBConsistencyLevel consistency_level, AllowSplitTablet allow_split_tablet,
      ReadResponseMsg* resp) = 0;

  // Called once per read before any work is done, so that an implementation can bound how many
  // reads it serves at a time. The returned token is held for the rest of the read and releases
  // the admission when it is destroyed, so it covers the asynchronous part of the read as well.
  //
  // Returning a failed Result rejects the read and sends that status to the client, so it must be
  // one the client retries. A bare ServiceUnavailable reaches the client as ERROR_SERVER_TOO_BUSY,
  // which RpcRetrier retries against the same peer under its own backoff policy.
  //
  // The default admits everything.
  virtual Result<std::shared_ptr<void>> AdmitRead(const ReadRequestMsg& req) {
    return std::shared_ptr<void>();
  }

  virtual ~ReadTabletProvider() = default;
};

void PerformRead(
    TabletServerIf* server, ReadTabletProvider* read_tablet_provider,
    const ReadRequestMsg* req, ReadResponseMsg* resp, rpc::RpcContext context);

}  // namespace tserver
}  // namespace yb
