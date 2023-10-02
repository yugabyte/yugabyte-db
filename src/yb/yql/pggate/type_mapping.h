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

// Provides mappings for argument and return types between YB C API and its C++ implementation.

#pragma once

#include "yb/util/status_fwd.h"

#include "yb/yql/pggate/util/ybc-internal.h"
#include "yb/yql/pggate/util/ybc_util.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

namespace yb {
namespace pggate {
namespace impl {

// ------------------------------------------------------------------------------------------------
// Mapping from C API return types to return types of actual C++ member function implementations.

// The type mapping is an identity mapping by default.
template<typename c_type> struct ReturnTypeMapper {
  typedef c_type cxx_ret_type;

  static c_type ToC(cxx_ret_type cxx_value) {
    return cxx_value;
  }
};

template<> struct ReturnTypeMapper<YBCStatus> {
  typedef Status cxx_ret_type;

  static YBCStatus ToC(cxx_ret_type cxx_value) {
    return ::yb::ToYBCStatus(cxx_value);
  }
};

// ------------------------------------------------------------------------------------------------
// Mapping from C API function argument types to argument types of actual C++ member functions.

// The type mapping is an identity mapping by default.
template<typename c_type> struct ArgTypeMapper {
  typedef c_type cxx_type;

  static cxx_type ToCxx(c_type c_value) {
    return c_value;
  }
};

// This template is specialized by C++ class generation macros to map an opaque struct pointer to
// a pointer to the implementation's C++ class.

} // namespace impl

} // namespace pggate
} // namespace yb
