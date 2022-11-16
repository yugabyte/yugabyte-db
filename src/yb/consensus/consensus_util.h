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

#include <float.h>

#include <chrono>
#include <sstream>
#include <string>
#include <type_traits>

#include <boost/mpl/and.hpp>

#include "yb/util/enums.h"
#include "yb/util/format.h"
#include "yb/util/math_util.h"
#include "yb/util/tostring.h"
#include "yb/util/type_traits.h"

namespace yb {
namespace consensus {

// Specifies whether to send empty consensus requests from the leader to followers in case the queue
// is empty.
YB_DEFINE_ENUM(
    RequestTriggerMode,

    // Only send a request if it is not empty.
    (kNonEmptyOnly)

    // Send a request even if the queue is empty, and therefore (in most cases) the request is
    // empty. This is used during heartbeats from leader to peers.
    (kAlwaysSend));

inline std::string MakeTabletLogPrefix(const std::string& tablet_id, const std::string& peer_id) {
  return Format("T $0 P $1: ", tablet_id, peer_id);
}

}  // namespace consensus
}  // namespace yb
