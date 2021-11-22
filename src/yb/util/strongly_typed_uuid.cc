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

#include "yb/util/strongly_typed_uuid.h"


namespace yb {

namespace {

// Makes transaction id from its binary representation.
// If check_exact_size is true, checks that slice contains only TransactionId.
Result<boost::uuids::uuid> DoDecodeUuid(
    const Slice &slice, const bool check_exact_size, const char* name) {
  if (check_exact_size ? slice.size() != boost::uuids::uuid::static_size()
                       : slice.size() < boost::uuids::uuid::static_size()) {
    if (!name) {
      name = "UUID";
    }
    return STATUS_FORMAT(
        Corruption, "Invalid length of binary data with $4 '$0': $1 (expected $2$3)",
        slice.ToDebugHexString(), slice.size(), check_exact_size ? "" : "at least ",
        boost::uuids::uuid::static_size(), name);
  }
  boost::uuids::uuid id;
  memcpy(id.data, slice.data(), boost::uuids::uuid::static_size());
  return id;
}

} // namespace

Result<boost::uuids::uuid> FullyDecodeUuid(const Slice& slice, const char* name) {
  return DoDecodeUuid(slice, /* check_exact_size= */ true, name);
}

boost::uuids::uuid TryFullyDecodeUuid(const Slice& slice) {
  if (slice.size() != boost::uuids::uuid::static_size()) {
    return boost::uuids::nil_uuid();
  }
  boost::uuids::uuid id;
  memcpy(id.data, slice.data(), boost::uuids::uuid::static_size());
  return id;
}

Result<boost::uuids::uuid> DecodeUuid(Slice* slice, const char* name) {
  auto id = VERIFY_RESULT(DoDecodeUuid(*slice, /* check_exact_size= */ false, name));
  slice->remove_prefix(boost::uuids::uuid::static_size());
  return id;
}

} // namespace yb
