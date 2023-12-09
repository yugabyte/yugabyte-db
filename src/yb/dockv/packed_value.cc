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

#include "yb/dockv/packed_value.h"

#include "yb/common/doc_hybrid_time.h"

#include "yb/dockv/primitive_value.h"
#include "yb/dockv/value_packing_v2.h"
#include "yb/dockv/value_type.h"

namespace yb::dockv {

Status PackedValueV1::FixValue() {
  // Remove buggy intent_doc_ht from start of the column. See #16650 for details.
  if (value_.TryConsumeByte(dockv::KeyEntryTypeAsChar::kHybridTime)) {
    RETURN_NOT_OK(DocHybridTime::EncodedFromStart(&value_));
  }
  if (value_.empty()) {
    value_ = *Null();
  }
  return Status::OK();
}

bool PackedValueV1::IsNull() const {
  return value_.empty() || static_cast<ValueEntryType>(value_[0]) == ValueEntryType::kTombstone;
}

PackedValueV1 PackedValueV1::Null() {
  static char null_column_type = dockv::ValueEntryTypeAsChar::kTombstone;
  return PackedValueV1(Slice(&null_column_type, sizeof(null_column_type)));
}

PackedValueV2 PackedValueV2::Null() {
  // Need a way to distinguish empty value and null value.
  // So null value has special slice for representation.
  return PackedValueV2(Slice(static_cast<const char*>(nullptr), static_cast<const char*>(nullptr)));
}

Result<QLValuePB> UnpackQLValue(PackedValueV1 value, DataType data_type) {
  if (value.IsNull()) {
    return QLValuePB();
  }
  QLValuePB result;
  RETURN_NOT_OK(dockv::PrimitiveValue::DecodeToQLValuePB(*value, data_type, &result));
  return result;
}

Result<PrimitiveValue> UnpackPrimitiveValue(PackedValueV1 value, DataType data_type) {
  dockv::PrimitiveValue pv;
  if (value.IsNull()) {
    return pv;
  }
  RETURN_NOT_OK(pv.DecodeFromValue(*value));
  return pv;
}

Result<PrimitiveValue> UnpackPrimitiveValue(PackedValueV2 value, DataType data_type) {
  // TODO(packed_row) direct unpack
  return PrimitiveValue::FromQLValuePB(VERIFY_RESULT(UnpackQLValue(value, data_type)));
}

}  // namespace yb::dockv
