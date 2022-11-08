// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/cdc/cdc_service.pb.h"
#include "yb/common/wire_protocol.h"

#include "yb/util/status_ec.h"

namespace yb {
namespace cdc {

struct CDCErrorTag : IntegralErrorTag<CDCErrorPB::Code> {
  // This category id is part of the wire protocol and should not be changed once released.
  static constexpr uint8_t kCategory = 16;

  typedef CDCErrorPB::Code Code;

  static const std::string& ToMessage(Code code) {
    return CDCErrorPB::Code_Name(code);
  }
};

typedef StatusErrorCodeImpl<CDCErrorTag> CDCError;

} // namespace cdc
} // namespace yb
