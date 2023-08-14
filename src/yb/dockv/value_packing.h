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

#include "yb/common/value.messages.h"

#include "yb/dockv/dockv_fwd.h"

namespace yb::dockv {

template <class F>
auto VisitDataType(DataType data_type, const F& f) {
  switch (data_type) {
    case DataType::BINARY:
      return f.Binary();
    case DataType::BOOL:
      return f.template Primitive<bool>();
    case DataType::FLOAT:
      return f.template Primitive<float>();
    case DataType::INT8:
      return f.template Primitive<int8_t>();
    case DataType::INT16:
      return f.template Primitive<int16_t>();
    case DataType::INT32:
      return f.template Primitive<int32_t>();
    case DataType::INT64:
      return f.template Primitive<int64_t>();
    case DataType::DECIMAL:
      return f.Decimal();
    case DataType::STRING:
      return f.String();
    case DataType::UINT32:
      return f.template Primitive<uint32_t>();
    case DataType::UINT64:
      return f.template Primitive<uint64_t>();
    case DataType::DOUBLE:
      return f.template Primitive<double>();
    default:
      break;
  }
  FATAL_INVALID_ENUM_VALUE(DataType, data_type);
}

}  // namespace yb::dockv
