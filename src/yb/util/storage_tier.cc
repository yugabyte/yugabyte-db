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

#include "yb/util/storage_tier.h"

#include "yb/util/flags.h"

DEFINE_NON_RUNTIME_PREVIEW_bool(enable_tiered_storage, false,
    "Enable tiered storage: storage-tier labels on --fs_data_dirs (path:tier), the storage_tier "
    "tablespace option, per-tablet tier_paths in the tablet superblock, tier-aware disk "
    "selection, the AlterTabletTier and GetTabletTierInfo RPCs and the tier migration "
    "reconciler. When disabled, tier labels, the storage_tier option and the RPCs are rejected, "
    "a target tier on tablet creation is ignored and no tiered-storage metadata is written. "
    "Tablets that already carry tier_paths keep honoring them so their SST files stay "
    "reachable; a target tier they already carry is kept but not acted on.");
