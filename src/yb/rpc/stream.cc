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

#include "yb/rpc/stream.h"

#include "yb/util/format.h"

namespace yb {
namespace rpc {

std::string Stream::ToString() const {
  return Format("{ local: $0 remote: $1 }", Local(), Remote());
}

}  // namespace rpc
}  // namespace yb
