//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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

#pragma once


namespace rocksdb {

#ifdef XFUNC

// DB-specific test points for the cross-functional test framework (see
// util/xfunc.h).
void xf_manage_release(ManagedIterator* iter);
void xf_manage_create(ManagedIterator* iter);
void xf_manage_new(DBImpl* db, ReadOptions* readoptions,
                   bool is_snapshot_supported);
void xf_transaction_write(const WriteOptions& write_options,
                          const DBOptions& db_options,
                          class WriteBatch* my_batch,
                          class WriteCallback* callback, DBImpl* db_impl,
                          Status* success, bool* write_attempted);

#endif  // XFUNC

}  // namespace rocksdb
