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

#ifdef XFUNC
#include "yb/rocksdb/util/xfunc.h"

#include <string>

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/utilities/optimistic_transaction_db.h"
#include "yb/rocksdb/write_batch.h"

namespace rocksdb {

std::string XFuncPoint::xfunc_test_;
bool XFuncPoint::initialized_ = false;
bool XFuncPoint::enabled_ = false;
int XFuncPoint::skip_policy_ = 0;

void GetXFTestOptions(Options* options, int skip_policy) {
  if (XFuncPoint::Check("inplace_lock_test") &&
      (!(skip_policy & kSkipNoSnapshot))) {
    options->inplace_update_support = true;
  }
}

void xf_manage_options(ReadOptions* read_options) {
  if (!XFuncPoint::Check("managed_xftest_dropold") &&
      (!XFuncPoint::Check("managed_xftest_release"))) {
    return;
  }
  read_options->managed = true;
}

void xf_transaction_set_memtable_history(
    int32_t* max_write_buffer_number_to_maintain) {
  *max_write_buffer_number_to_maintain = 10;
}

void xf_transaction_clear_memtable_history(
    int32_t* max_write_buffer_number_to_maintain) {
  *max_write_buffer_number_to_maintain = 0;
}

}  // namespace rocksdb

#endif  // XFUNC
