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

#include "yb/rpc/sidecars.h"

#include "yb/gutil/casts.h"

#include "yb/util/flags/flag_tags.h"
#include "yb/util/size_literals.h"

using namespace yb::size_literals;

DEFINE_UNKNOWN_uint64(min_sidecar_buffer_size, 16_KB, "Minimal buffer to allocate for sidecar");

namespace yb::rpc {

Sidecars::Sidecars(ScopedTrackedConsumption* consumption)
    : buffer_(FLAGS_min_sidecar_buffer_size, consumption) {
}

WriteBuffer& Sidecars::Start() {
  offsets_.Add(narrow_cast<uint32_t>(buffer_.size()));
  return buffer_;
}

size_t Sidecars::Complete() {
  return offsets_.size() - 1;
}

Status Sidecars::AssignTo(int idx, std::string* out) {
  auto end = idx + 1 < offsets_.size() ? offsets_[idx + 1] : buffer_.size();
  buffer_.AssignTo(offsets_[idx], end, out);
  return Status::OK();
}

size_t Sidecars::Transfer(Sidecars* dest) {
  return dest->Take(&buffer_, &offsets_);
}

size_t Sidecars::Take(
    WriteBuffer* buffer, google::protobuf::RepeatedField<uint32_t>* offsets) {
  std::lock_guard lock(take_mutex_);
  uint32_t base_offset = narrow_cast<uint32_t>(buffer_.size());
  buffer_.Take(buffer);
  auto result = offsets_.size();
  if (!base_offset && !result) {
    offsets_.Swap(offsets);
  } else {
    offsets_.Reserve(offsets_.size() + offsets->size());
    for (auto offset : *offsets) {
      offsets_.Add(base_offset + offset);
    }
    offsets->Clear();
  }
  return result;
}

size_t Sidecars::Take(
    const RefCntBuffer& buffer,
    const boost::container::small_vector_base<const uint8_t*>& bounds) {
  std::lock_guard lock(take_mutex_);
  auto result = offsets_.size();
  if (bounds.empty()) {
    return result;
  }
  auto base_offset = narrow_cast<uint32_t>(buffer_.size());
  const auto* base = bounds.front();
  buffer_.AddBlock(buffer, base - buffer.udata());
  for (auto it = bounds.begin(), end = --bounds.end(); it != end; ++it) {
    offsets_.Add(narrow_cast<uint32_t>(base_offset + *it - base));
  }
  return result;
}

void Sidecars::Reset() {
  buffer_.Reset();
  offsets_.Clear();
}

Slice Sidecars::GetFirst() const {
  size_t size = offsets_.size() > 1 ? offsets_[1] : buffer_.size();
  return Slice(buffer_.FirstBlockData(), size);
}

RefCntSlice Sidecars::Extract(size_t index) const {
  auto next_index = narrow_cast<int>(index + 1);
  size_t end = next_index < offsets_.size()
      ? offsets_[next_index] : buffer_.size();
  return buffer_.ExtractContinuousBlock(offsets_[narrow_cast<int>(index)], end);
}

void Sidecars::MoveOffsetsTo(size_t body_size, google::protobuf::RepeatedField<uint32_t>* dest) {
  for (auto& offset : offsets_) {
    offset += body_size;
  }
  *dest = std::move(offsets_);
}

void Sidecars::Flush(ByteBlocks* output) {
  buffer_.Flush(output);
}

void Sidecars::CopyTo(std::byte* out) {
  buffer_.CopyTo(out);
}

} // namespace yb::rpc
