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

#include "yb/util/flags.h"

DEFINE_RUNTIME_int32(compaction_priority_start_bound, 10,
    "Compaction task of DB that has number of SST files less than specified will have "
    "priority 0.");

DEFINE_RUNTIME_int32(compaction_priority_step_size, 5,
    "Compaction task of DB that has number of SST files greater that "
    "compaction_priority_start_bound will get 1 extra priority per every "
    "compaction_priority_step_size files.");
