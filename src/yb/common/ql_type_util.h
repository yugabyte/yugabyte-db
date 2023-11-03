//--------------------------------------------------------------------------------------------------
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
//
// This module is to define a few supporting functions for QLTYPE.
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/common/common.pb.h"
#include "yb/common/value.pb.h"
#include "yb/util/status.h"

namespace yb {

// Iterating via UDTypeInfo in QLTypePB in Read-only mode.
template <typename Fn> // Status fn(const QLTypePB::UDTypeInfo&)
Status IterateAndDoForUDT(const QLTypePB& pb_type, Fn fn) {
  if (pb_type.main() == USER_DEFINED_TYPE) {
    RETURN_NOT_OK(fn(pb_type.udtype_info()));
  }

  for (int i = 0; i < pb_type.params_size(); ++i) {
    RETURN_NOT_OK(IterateAndDoForUDT(pb_type.params(i), fn));
  }

  return Status::OK();
}

// Iterating via UDTypeInfo in QLTypePB for the data updating.
template <typename Fn> // Status fn(QLTypePB::UDTypeInfo*)
Status IterateAndDoForUDT(QLTypePB* pb_type, Fn fn) {
  if (pb_type->main() == USER_DEFINED_TYPE) {
    RETURN_NOT_OK(fn(pb_type->mutable_udtype_info()));
  }

  for (int i = 0; i < pb_type->params_size(); ++i) {
    RETURN_NOT_OK(IterateAndDoForUDT(pb_type->mutable_params(i), fn));
  }

  return Status::OK();
}

}; // namespace yb
