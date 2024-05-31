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


#include "yb/rocksdb/util/options_sanity_check.h"

namespace rocksdb {

namespace {
OptionsSanityCheckLevel SanityCheckLevelHelper(
    const std::unordered_map<std::string, OptionsSanityCheckLevel>& smap,
    const std::string& name) {
  auto iter = smap.find(name);
  return iter != smap.end() ? iter->second : kSanityLevelExactMatch;
}
}

OptionsSanityCheckLevel DBOptionSanityCheckLevel(
    const std::string& option_name) {
  return SanityCheckLevelHelper(sanity_level_db_options, option_name);
}

OptionsSanityCheckLevel CFOptionSanityCheckLevel(
    const std::string& option_name) {
  return SanityCheckLevelHelper(sanity_level_cf_options, option_name);
}

OptionsSanityCheckLevel BBTOptionSanityCheckLevel(
    const std::string& option_name) {
  return SanityCheckLevelHelper(sanity_level_bbt_options, option_name);
}

}  // namespace rocksdb
