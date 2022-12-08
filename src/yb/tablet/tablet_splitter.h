//
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

#include <type_traits>

#include "yb/consensus/consensus_fwd.h"
#include "yb/consensus/log_fwd.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/util/status_fwd.h"

namespace yb {
namespace tablet {

// Interface which provides functionality for applying Raft tablet split operation.
// Used by TabletPeer to avoid introducing backward dependency on tserver component.
class TabletSplitter {
 public:
  TabletSplitter() = default;

  TabletSplitter(const TabletSplitter&) = delete;
  void operator=(const TabletSplitter&) = delete;

  virtual ~TabletSplitter() = default;

  // Implementation should apply tablet split Raft operation.
  // state is a context of operation to apply.
  // If raft_log and raft_config are specified - they will be used as a source tablet Raft log and
  // Raft config (during tablet bootstrap tablet peer's Raft consensus is not yet initialized,
  // so we pass raft_log and raft_config explicitly).
  // If these arguments are not specified, it's assumed that the Raft log and Raft config are
  // accessible from tablet peer's Raft consensus instance and initialized.
  virtual Status ApplyTabletSplit(
      SplitOperation* operation, log::Log* raft_log,
      boost::optional<consensus::RaftConfigPB> raft_config) = 0;
};

}  // namespace tablet
}  // namespace yb
