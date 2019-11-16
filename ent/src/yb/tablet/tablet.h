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

#include <mutex>

#include "../../../../src/yb/tablet/tablet.h"

#include "yb/gutil/ref_counted.h"
#include "yb/util/string_util.h"

namespace yb {
namespace tablet {

class SnapshotOperationState;

namespace enterprise {

class TabletScopedIf : public RefCountedThreadSafe<TabletScopedIf> {
 public:
  virtual std::string Key() const = 0;
 protected:
  friend class RefCountedThreadSafe<TabletScopedIf>;
  virtual ~TabletScopedIf() { }
};

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

  // Allows us to add tablet-specific information that will get deref'd when the tablet does.
  void add_additional_metadata(scoped_refptr<TabletScopedIf> additional_metadata) {
    std::lock_guard<std::mutex> lock(control_path_mutex_);
    additional_metadata_[additional_metadata->Key()] = additional_metadata;
  }

  scoped_refptr<TabletScopedIf> get_additional_metadata(const std::string& key) {
    std::lock_guard<std::mutex> lock(control_path_mutex_);
    auto val = additional_metadata_.find(key);
    return (val != additional_metadata_.end()) ? val->second : nullptr;
  }

 protected:
  CHECKED_STATUS CreateTabletDirectories(const string& db_dir, FsManager* fs) override;

  mutable std::mutex control_path_mutex_;
  std::unordered_map<std::string, scoped_refptr<TabletScopedIf> > additional_metadata_
    GUARDED_BY(control_path_mutex_);

 private:
  DISALLOW_COPY_AND_ASSIGN(Tablet);
};

}  // namespace enterprise
}  // namespace tablet
}  // namespace yb

#endif  // ENT_SRC_YB_TABLET_TABLET_H
