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

#pragma once

#include <random>

#include "yb/dockv/dockv_fwd.h"

namespace yb::dockv {

using RandomNumberGenerator = std::mt19937_64;

ValueEntryType GenRandomPrimitiveValue(RandomNumberGenerator* rng, QLValuePB* holder);

PrimitiveValue GenRandomPrimitiveValue(RandomNumberGenerator* rng);

// Generate a "minimal" DocKey.
DocKey CreateMinimalDocKey(RandomNumberGenerator* rng, UseHash use_hash);

// Generate a random DocKey with up to the default number of components.
DocKey GenRandomDocKey(RandomNumberGenerator* rng, UseHash use_hash);

std::vector<DocKey> GenRandomDocKeys(
    RandomNumberGenerator* rng, UseHash use_hash, int num_keys);

std::vector<SubDocKey> GenRandomSubDocKeys(
    RandomNumberGenerator* rng, UseHash use_hash, int num_keys);

KeyEntryValue GenRandomKeyEntryValue(RandomNumberGenerator* rng);

std::vector<KeyEntryValue> GenRandomKeyEntryValues(RandomNumberGenerator* rng, int max_num = 10);

QLValuePB RandomQLValue(DataType type);

}  // namespace yb::dockv
