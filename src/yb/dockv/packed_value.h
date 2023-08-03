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

#include "yb/dockv/dockv_fwd.h"

#include "yb/util/slice.h"
#include "yb/util/status.h"

namespace yb::dockv {

class PackedValueV1 {
 public:
  explicit PackedValueV1(Slice value) : value_(value) {}

  Slice* operator->() {
    return &value_;
  }

  Slice& operator*() {
    return value_;
  }

  const Slice* operator->() const {
    return &value_;
  }

  const Slice& operator*() const {
    return value_;
  }

  // Adjust the value by working around the effects of bugs that were present in earlier revisions.
  Status FixValue();

  bool IsNull() const;

  static PackedValueV1 Null();

 private:
  Slice value_;
};

class PackedValueV2 {
 public:
  explicit PackedValueV2(Slice value) : value_(value) {}

  Slice* operator->() {
    return &value_;
  }

  Slice& operator*() {
    return value_;
  }

  const Slice* operator->() const {
    return &value_;
  }

  const Slice& operator*() const {
    return value_;
  }

  Status FixValue() {
    return Status::OK();
  }

  bool IsNull() const {
    return !value_.data();
  }

  static PackedValueV2 Null();

 private:
  Slice value_;
};

Result<QLValuePB> UnpackQLValue(PackedValueV1 value, DataType data_type);

Result<PrimitiveValue> UnpackPrimitiveValue(PackedValueV1 value, DataType data_type);
Result<PrimitiveValue> UnpackPrimitiveValue(PackedValueV2 value, DataType data_type);

}  // namespace yb::dockv
