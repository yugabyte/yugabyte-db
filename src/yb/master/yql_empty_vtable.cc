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

#include "yb/master/master_defaults.h"
#include "yb/master/yql_empty_vtable.h"

namespace yb {
namespace master {

YQLEmptyVTable::YQLEmptyVTable(const TableName& table_name,
                               const Master* const master,
                               const Schema& schema)
    : YQLVirtualTable(table_name, master, schema) {
}

Status YQLEmptyVTable::RetrieveData(const QLReadRequestPB& request,
                                    std::unique_ptr<QLRowBlock>* vtable) const {
  // Empty rowblock.
  vtable->reset(new QLRowBlock(schema_));
  return Status::OK();
}

}  // namespace master
}  // namespace yb
