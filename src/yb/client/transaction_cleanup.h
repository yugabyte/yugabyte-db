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

#include <vector>

#include "yb/common/transaction.h"
#include "yb/common/entity_ids_types.h"
#include "yb/util/strongly_typed_bool.h"

template <class T> class scoped_refptr;

namespace yb {
class ClockBase;

namespace client {

YB_STRONGLY_TYPED_BOOL(Sealed);
class YBClient;

// Sends cleanup intents request to provided tablets.
// sealed - whether transaction was previously sealed.
void CleanupTransaction(
    YBClient* client, const scoped_refptr<ClockBase>& clock, const TransactionId& transaction_id,
    Sealed sealed, CleanupType type, const std::vector<TabletId>& tablets);

} // namespace client
} // namespace yb
