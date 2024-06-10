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

#include <stdint.h>

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include <boost/container/stable_vector.hpp>
#include <boost/range/iterator_range.hpp>
#include "yb/util/flags.h"

#include "yb/client/client_fwd.h"

#include "yb/common/common_fwd.h"
#include "yb/common/entity_ids.h"
#include "yb/common/transaction.h"

#include "yb/gutil/integral_types.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/util/shared_lock.h"

namespace yb {
namespace client {

YB_STRONGLY_TYPED_BOOL(Sealed);

// Sends cleanup intents request to provided tablets.
// sealed - whether transaction was previously sealed.
void CleanupTransaction(
    YBClient* client, const scoped_refptr<ClockBase>& clock, const TransactionId& transaction_id,
    Sealed sealed, CleanupType type, const std::vector<TabletId>& tablets);

} // namespace client
} // namespace yb
