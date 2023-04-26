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

#include "yb/common/common_fwd.h"
#include "yb/common/common_types.pb.h"

namespace yb {

class WriteBuffer;

}

namespace yb::qlexpr {

void SerializeValue(
    const std::shared_ptr<QLType>& ql_type, const QLClient& client, const QLValuePB& pb,
    WriteBuffer* buffer);

}  // namespace yb::qlexpr
