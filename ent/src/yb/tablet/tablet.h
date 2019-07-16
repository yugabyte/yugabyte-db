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

#ifndef ENT_SRC_YB_TABLET_TABLET_H
#define ENT_SRC_YB_TABLET_TABLET_H

#include "../../../../src/yb/tablet/tablet.h"

#include "yb/util/string_util.h"

namespace yb {
namespace tablet {

class SnapshotOperationState;

namespace enterprise {

static const std::string kTempSnapshotDirSuffix = ".tmp";

class Tablet : public yb::tablet::Tablet {
  typedef yb::tablet::Tablet super;
 public:
  // Create a new tablet.
  template <class... Args>
  explicit Tablet(Args&&... args)
      : super(std::forward<Args>(args)...) {}

  // Create snapshot for this tablet.
  CHECKED_STATUS CreateSnapshot(SnapshotOperationState* tx_state) override;

  // Delete snapshot for this tablet.
  CHECKED_STATUS DeleteSnapshot(SnapshotOperationState* tx_state) override;

  static bool IsTempSnapshotDir(const std::string& dir) {
    return StringEndsWith(dir, kTempSnapshotDirSuffix);
  }

 protected:
  CHECKED_STATUS CreateTabletDirectories(const string& db_dir, FsManager* fs) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(Tablet);
};

}  // namespace enterprise
}  // namespace tablet
}  // namespace yb

#endif  // ENT_SRC_YB_TABLET_TABLET_H
