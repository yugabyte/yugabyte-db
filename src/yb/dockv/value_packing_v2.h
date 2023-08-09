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

#include "yb/common/value.messages.h"

#include "yb/dockv/dockv_fwd.h"

#include "yb/util/kv_util.h"

namespace yb::dockv {

void PackQLValueV2(const QLValuePB& value, DataType data_type, ValueBuffer* out);
void PackQLValueV2(const LWQLValuePB& value, DataType data_type, ValueBuffer* out);
Result<QLValuePB> UnpackQLValue(PackedValueV2 value, DataType data_type);

ssize_t PackedQLValueSizeV2(const QLValuePB& value, DataType data_type);
ssize_t PackedQLValueSizeV2(const LWQLValuePB& value, DataType data_type);

bool PackedAsVarlen(DataType data_type);

}  // namespace yb::dockv
