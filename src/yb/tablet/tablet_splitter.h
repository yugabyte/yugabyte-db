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

#ifndef YB_TABLET_TABLET_SPLITTER_H
#define YB_TABLET_TABLET_SPLITTER_H

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
  // If raft_log is specified - it will be used as a source tablet Raft log (during tablet bootstrap
  // tablet peer's Raft consensus is not yet initialized, so we pass raft_log explicitly).
  virtual CHECKED_STATUS ApplyTabletSplit(SplitOperation* operation, log::Log* raft_log) = 0;
};

}  // namespace tablet
}  // namespace yb

#endif /* YB_TABLET_TABLET_SPLITTER_H */
