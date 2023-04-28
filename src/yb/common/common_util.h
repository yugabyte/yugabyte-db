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

#include "yb/common/common_types.pb.h"

namespace yb {

// These functions calculate the initial number of tablets per tablet on base of the specified
// number of tservers specified, flags (ysql_num_shards_per_tserver, yb_num_shards_per_tserver
// and enable_automatic_tablet_splitting) and number of CPU cores.
// Special case: 0 is returned if tserver_count is specified as 0; the caller is responsible to
// handle this case by itself.
int GetInitialNumTabletsPerTable(YQLDatabase db_type, size_t tserver_count);
int GetInitialNumTabletsPerTable(TableType table_type, size_t tserver_count);

} // namespace yb
