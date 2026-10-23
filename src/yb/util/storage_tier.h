// Copyright (c) YugabyteDB, Inc.
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

#include <algorithm>
#include <string>
#include <vector>

namespace yb {

// Tiered storage: the fixed, predefined set of storage-tier labels understood by the system.
// A tserver's --fs_data_dirs may tag each data directory with one of these labels, and a
// tablespace's storage_tier option (see TablespaceParser::PlacementInfoFromJson) is validated
// against the same set. A tablespace naming any other tier could never be satisfied by any
// tserver, so it's rejected at DDL time rather than left to fail silently later.
//
// kDefaultStorageTier is always a member of this set, and data roots in --fs_data_dirs that
// carry no explicit ":tier" suffix fall back to it, so existing/unlabeled deployments keep
// working.
//
// The order below is significant: it doubles as the tier preference
// order for policies that need to pick "the fastest tier with disks" (e.g. WAL directory
// defaulting in FsManager::Init()), so keep the fastest/most-preferred tier first.
constexpr const char* kDefaultStorageTier = "ssd";

inline const std::vector<std::string>& ValidStorageTiers() {
  static const std::vector<std::string> kTiers = {"ssd", "hdd"};
  return kTiers;
}

// Whether `tier` is one of ValidStorageTiers().
inline bool IsValidStorageTier(const std::string& tier) {
  const auto& tiers = ValidStorageTiers();
  return std::find(tiers.begin(), tiers.end(), tier) != tiers.end();
}

}  // namespace yb
