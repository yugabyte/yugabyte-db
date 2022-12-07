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

// Portions Copyright (c) YugaByte, Inc.

#pragma once

#include <float.h>

#include <chrono>
#include <sstream>
#include <string>
#include <type_traits>

#include <boost/mpl/and.hpp>
#include <google/protobuf/stubs/port.h>
#include <google/protobuf/generated_enum_reflection.h>
#include <google/protobuf/message_lite.h>

#include "yb/util/enums.h"
#include "yb/util/format.h"
#include "yb/util/math_util.h"
#include "yb/util/tostring.h"
#include "yb/util/type_traits.h"

#define PB_ENUM_FORMATTERS(EnumType) \
  inline std::string PBEnumToString(EnumType value) { \
    if (BOOST_PP_CAT(EnumType, _IsValid)(value)) { \
      return BOOST_PP_CAT(EnumType, _Name)(value); \
    } else { \
      return ::yb::Format("<unknown " BOOST_PP_STRINGIZE(EnumType) " : $0>", \
          ::yb::to_underlying(value)); \
    } \
  } \
  inline std::string ToString(EnumType value) { \
    return PBEnumToString(value); \
  } \
  __attribute__((unused)) inline std::ostream& operator << (std::ostream& out, EnumType value) { \
    return out << PBEnumToString(value); \
  }

namespace yb {

template<typename T>
std::vector<T> GetAllPbEnumValues() {
  const auto* desc = google::protobuf::GetEnumDescriptor<T>();
  std::vector<T> result;
  result.reserve(desc->value_count());
  for (int i = 0; i < desc->value_count(); ++i) {
    result.push_back(T(desc->value(i)->number()));
  }
  return result;
}

template <class Source, class Dest>
void AppendToRepeated(const Source& source, Dest* dest) {
  dest->Reserve(dest->size() + source.size());
  for (const auto& elem : source) {
    dest->Add(elem);
  }
}

} // namespace yb
