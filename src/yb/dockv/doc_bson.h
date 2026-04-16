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

#include "yb/util/kv_util.h"
#include "yb/util/slice.h"

namespace yb::dockv {

void BsonKeyToComparableBinary(Slice slice, KeyBuffer& dest);

void BsonKeyToComparableBinaryDescending(Slice slice, KeyBuffer& dest);

Status BsonKeyFromComparableBinary(Slice* slice, std::string* result);
Result<const char*> BsonKeyFromComparableBinary(
    const char* begin, const char* end, ValueBuffer* out);
Result<const char*> SkipComparableBson(const char* begin, const char* end);

Status BsonKeyFromComparableBinaryDescending(Slice* slice, std::string* result);
Result<const char*> BsonKeyFromComparableBinaryDescending(
    const char* begin, const char* end, ValueBuffer* out);
Result<const char*> SkipComparableBsonDescending(const char* begin, const char* end);

} // namespace yb::dockv
