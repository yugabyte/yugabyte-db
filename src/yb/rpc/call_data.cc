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

#include "yb/rpc/call_data.h"

#include <google/protobuf/repeated_field.h>

#include "yb/rpc/sidecars.h"

namespace yb::rpc {

Result<RefCntSlice> ReceivedSidecars::Extract(const RefCntBuffer& buffer, size_t idx) const {
  SCHECK_LT(idx + 1, sidecar_bounds_.size(), InvalidArgument, "Sidecar out of bounds");
  return RefCntSlice(buffer, Slice(sidecar_bounds_[idx], sidecar_bounds_[idx + 1]));
}

Result<SidecarHolder> ReceivedSidecars::GetHolder(const RefCntBuffer& buffer, size_t idx) const {
  return SidecarHolder(buffer, Slice(sidecar_bounds_[idx], sidecar_bounds_[idx + 1]));
}

size_t ReceivedSidecars::Transfer(const RefCntBuffer& buffer, Sidecars* dest) {
  return dest->Take(buffer, sidecar_bounds_);
}

Status ReceivedSidecars::Parse(
    Slice message, const boost::iterator_range<const uint32_t*>& offsets) {
  sidecar_bounds_.reserve(offsets.size() + 1);

  uint32_t prev_offset = 0;
  for (auto offset : offsets) {
    if (offset > message.size() || offset < prev_offset) {
      return STATUS_FORMAT(
          Corruption,
          "Invalid sidecar offsets; sidecar apparently starts at $0,"
          " ends at $1, but the entire message has length $2",
          prev_offset, offset, message.size());
    }
    sidecar_bounds_.push_back(message.data() + offset);
    prev_offset = offset;
  }
  sidecar_bounds_.emplace_back(message.end());

  return Status::OK();
}

}  // namespace yb::rpc
