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

#ifndef YB_TABLET_TABLET_RETENTION_POLICY_H_
#define YB_TABLET_TABLET_RETENTION_POLICY_H_

#include "yb/docdb/docdb_compaction_filter.h"
#include "yb/server/clock.h"

#include "yb/tablet/tablet_fwd.h"

namespace yb {
namespace tablet {

// History retention policy used by a tablet. It is based on pending reads and a fixed retention
// interval configured by the user.
class TabletRetentionPolicy : public docdb::HistoryRetentionPolicy {
 public:
  explicit TabletRetentionPolicy(Tablet* tablet);

  docdb::HistoryRetentionDirective GetRetentionDirective() override;

 private:
  Tablet* tablet_;

  // The delta to be added to the current time to get the history cutoff timestamp. This is always
  // a negative amount.
  MonoDelta retention_delta_;
};

}  // namespace tablet
}  // namespace yb

#endif  // YB_TABLET_TABLET_RETENTION_POLICY_H_
