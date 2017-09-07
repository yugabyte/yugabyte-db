// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <mutex>

#include "util/thread_status_updater.h"
#include "db/column_family.h"

namespace rocksdb {

#ifndef NDEBUG
#if ROCKSDB_USING_THREAD_STATUS
void ThreadStatusUpdater::TEST_VerifyColumnFamilyInfoMap(
    const std::vector<ColumnFamilyHandle*>& handles,
    bool check_exist) {
  std::unique_lock<std::mutex> lock(thread_list_mutex_);
  if (check_exist) {
    assert(cf_info_map_.size() == handles.size());
  }
  for (auto* handle : handles) {
    auto* cfd = reinterpret_cast<ColumnFamilyHandleImpl*>(handle)->cfd();
    auto iter __attribute__((unused)) = cf_info_map_.find(cfd);
    if (check_exist) {
      assert(iter != cf_info_map_.end());
      assert(iter->second);
      assert(iter->second->cf_name == cfd->GetName());
    } else {
      assert(iter == cf_info_map_.end());
    }
  }
}

#else

void ThreadStatusUpdater::TEST_VerifyColumnFamilyInfoMap(
    const std::vector<ColumnFamilyHandle*>& handles,
    bool check_exist) {
}

#endif  // ROCKSDB_USING_THREAD_STATUS
#endif  // !NDEBUG


}  // namespace rocksdb
