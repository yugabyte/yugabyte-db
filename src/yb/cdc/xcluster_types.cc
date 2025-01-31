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

#include "yb/cdc/xcluster_types.h"

#include "yb/util/format.h"

namespace yb::xcluster {

std::string ProducerTabletInfo::ToString() const {
  return YB_STRUCT_TO_STRING(replication_group_id, stream_id, tablet_id, table_id);
}

std::string ConsumerTabletInfo::ToString() const {
  return Format("{ consumer tablet ID: $0 table ID: $1 }", tablet_id, table_id);
}

}  // namespace yb::xcluster
