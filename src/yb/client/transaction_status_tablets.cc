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
#include "yb/client/transaction_status_tablets.h"

#include "yb/util/tostring.h"

namespace yb::client {

std::string TransactionStatusTablets::TablespaceInfo::ToString() const {
  return YB_STRUCT_TO_STRING(placement_info, tablets);
}

std::string TransactionStatusTablets::ToString() const {
  return YB_STRUCT_TO_STRING(global_tablets, region_local_tablets, tablespaces);
}

} // namespace yb::client
