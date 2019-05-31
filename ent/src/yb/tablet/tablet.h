// Copyright (c) YugaByte, Inc.

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
