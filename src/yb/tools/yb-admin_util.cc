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

#include "yb/tools/yb-admin_util.h"

#include "yb/common/snapshot.h"

#include "yb/util/result.h"

namespace yb {
namespace tools {

using std::string;

string SnapshotIdToString(const SnapshotId& snapshot_id) {
  auto txn_snapshot_id = TryFullyDecodeTxnSnapshotId(snapshot_id);
  return txn_snapshot_id ? txn_snapshot_id.ToString() : snapshot_id;
}

SnapshotId StringToSnapshotId(const string& str) {
  if (str.length() == TxnSnapshotId::StaticStringSize()) {
    auto txn_snapshot_id = TxnSnapshotIdFromString(str);
    if (txn_snapshot_id.ok()) {
      return SnapshotId(to_char_ptr(txn_snapshot_id->data()), txn_snapshot_id->size());
    }
  }
  // If conversion into TxnSnapshotId failed.
  return SnapshotId(str);
}

}  // namespace tools
}  // namespace yb
