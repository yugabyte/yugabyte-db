// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
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
#include <vector>
#include <string>

#include "yb/rocksdb/utilities/stackable_db.h"
#include "yb/rocksdb/utilities/db_ttl.h"
#include "yb/rocksdb/db.h"

namespace rocksdb {

// Please don't use this class. It's deprecated
class UtilityDB {
 public:
  // This function is here only for backwards compatibility. Please use the
  // functions defined in DBWithTTl (rocksdb/utilities/db_ttl.h)
  // (deprecated)
#if defined(__GNUC__) || defined(__clang__)
  __attribute__((deprecated))
#elif _WIN32
   __declspec(deprecated)
#endif
    static Status OpenTtlDB(const Options& options,
                                                      const std::string& name,
                                                      StackableDB** dbptr,
                                                      int32_t ttl = 0,
                                                      bool read_only = false);
};

} //  namespace rocksdb
