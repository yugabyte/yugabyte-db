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

#pragma once

#include "yb/common/entity_ids_types.h"
#include "yb/master/tablet_split_fwd.h"
#include "yb/util/status.h"

namespace yb {
namespace master {

class CDCSplitDriverIf {
 public:
  virtual ~CDCSplitDriverIf() {}
  virtual Status UpdateXClusterConsumerOnTabletSplit(
      const TableId& consumer_table_id, const SplitTabletIds& split_tablet_ids) = 0;

  virtual Status UpdateCDCProducerOnTabletSplit(
      const TableId& producer_table_id, const SplitTabletIds& split_tablet_ids) = 0;
};

} // namespace master
} // namespace yb
