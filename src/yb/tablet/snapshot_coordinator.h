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

#include "yb/tablet/tablet_fwd.h"

#include "yb/util/status_fwd.h"

namespace yb {

class Slice;

namespace tablet {

// Interface for snapshot coordinator.
class SnapshotCoordinator {
 public:
  virtual Status CreateReplicated(int64_t leader_term, const SnapshotOperation& operation) = 0;

  virtual Status DeleteReplicated(int64_t leader_term, const SnapshotOperation& operation) = 0;

  virtual Status RestoreSysCatalogReplicated(
      int64_t leader_term, const SnapshotOperation& operation, Status* complete_status) = 0;

  virtual Status Load(Tablet* tablet) = 0;

  virtual Status ApplyWritePair(const Slice& key, const Slice& value) = 0;

  virtual ~SnapshotCoordinator() = default;
};

} // namespace tablet
} // namespace yb
