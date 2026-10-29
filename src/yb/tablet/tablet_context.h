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

#pragma once

#include "yb/common/opid.h"

#include "yb/consensus/log_fwd.h"

namespace yb::tablet {

// Interface through which Tablet uses the TabletPeer owning it.
class TabletContext {
 public:
  virtual ~TabletContext() = default;

  // See log::Log::WalState.
  virtual log::WalEntryState WalState(const OpId& op_id) = 0;
};

// Context of a tablet not owned by a TabletPeer, e.g. one opened by a tool or a test.
class StandaloneTabletContext : public TabletContext {
 public:
  // There is no WAL to wait for, so flushes are not constrained.
  log::WalEntryState WalState(const OpId&) override {
    return log::WalEntryState::kDurable;
  }

  static StandaloneTabletContext& Instance() {
    static StandaloneTabletContext instance;
    return instance;
  }
};

} // namespace yb::tablet
