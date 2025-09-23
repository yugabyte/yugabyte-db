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

#include "yb/master/master_types.pb.h"

#include "yb/util/status_fwd.h"
#include "yb/util/status_ec.h"

namespace yb {
namespace master {

struct MasterErrorTag : IntegralErrorTag<MasterErrorPB::Code> {
  // This category id is part of the wire protocol and should not be changed once released.
  static constexpr uint8_t kCategory = 9;

  static const std::string& ToMessage(Value code) {
    return MasterErrorPB::Code_Name(code);
  }
};

typedef StatusErrorCodeImpl<MasterErrorTag> MasterError;

} // namespace master
} // namespace yb
