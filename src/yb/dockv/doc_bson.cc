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

#include "yb/util/result.h"

#include "yb/dockv/doc_bson.h"
#include "yb/dockv/doc_kv_util.h"

namespace yb::dockv {

// YB_TODO: Currently mapping BSON keys to string encoding, which is not correct. These functions
// need to be updated, so that it gets converted such that the binary representation matches the
// BSON sort order.

void BsonKeyToComparableBinary(Slice slice, KeyBuffer& dest) {
  ZeroEncodeAndAppendStrToKey(slice, dest);
}

void BsonKeyToComparableBinaryDescending(Slice slice, KeyBuffer& dest) {
  ComplementZeroEncodeAndAppendStrToKey(slice, dest);
}

Status BsonKeyFromComparableBinary(Slice* slice, std::string* result) {
  return DecodeZeroEncodedStr(slice, result);
}

Result<const char*> BsonKeyFromComparableBinary(
    const char* begin, const char* end, ValueBuffer* out) {
  return DecodeZeroEncodedStr(begin, end, out);
}

Result<const char*> SkipComparableBson(const char* begin, const char* end) {
  return SkipZeroEncodedStr(begin, end);
}

Status BsonKeyFromComparableBinaryDescending(Slice* slice, std::string* result) {
  return DecodeComplementZeroEncodedStr(slice, result);
}

Result<const char*> BsonKeyFromComparableBinaryDescending(
    const char* begin, const char* end, ValueBuffer* out) {
  return DecodeComplementZeroEncodedStr(begin, end, out);
}

Result<const char*> SkipComparableBsonDescending(const char* begin, const char* end) {
  return SkipComplementZeroEncodedStr(begin, end);
}

}  // namespace yb::dockv
