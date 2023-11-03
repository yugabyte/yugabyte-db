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

#include "yb/common/common_fwd.h"

#include "yb/util/enums.h"

namespace yb {

// As a result of single dynamic tablet splitting operation we always have kNumSplitParts new
// post-split tablets.
constexpr const auto kNumSplitParts = 2;

constexpr const ColocationId kColocationIdNotSet = 0;

// Minimum colocation ID to be auto-generated, IDs below are reserved just in case we want
// some special values in the future.
// This has been chosen to match FirstNormalObjectId from Postgres code.
constexpr const ColocationId kFirstNormalColocationId = 16384;

YB_DEFINE_ENUM(SortingType,
               (kNotSpecified)
               (kAscending)           // ASC, NULLS FIRST
               (kDescending)          // DESC, NULLS FIRST
               (kAscendingNullsLast)  // ASC, NULLS LAST
               (kDescendingNullsLast) // DESC, NULLS LAST
);

static const char* const kObsoleteShortPrimaryTableId = "sys.catalog.uuid";

constexpr auto kPitrFeatureName = "PITR";

} // namespace yb
