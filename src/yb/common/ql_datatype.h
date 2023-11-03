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
#include "yb/common/value.pb.h"

namespace yb {

// The value type.
typedef QLValuePB::ValueCase InternalType;

DataType InternalToDataType(InternalType internal_type);
std::string InternalTypeToCQLString(InternalType internal_type);

} // namespace yb
