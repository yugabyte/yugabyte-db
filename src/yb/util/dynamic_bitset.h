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

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace yb {

// A bit set sized with resize(), as std::vector, holding up to kInlineCapacity bits without
// allocating.
//
// Virtual (unbounded) state: no bits are stored and every bit tests as the same value - unset for
// a default-constructed set, set for All(). resize() bounds a virtual set, keeping that value for
// every bit. A bounded set has a size; test() and set() CHECK that the position is below it.
// resize() of a bounded set grows it with the new bits unset, or shrinks it keeping the allocation.
//
// Two words: control_ holds the representation flags and the size, storage_ holds the bits inline
// or points to them on the heap. The representation invariants are in dynamic_bitset.cc; keep them
// when extending this class.
class DynamicBitSet {
 private:
  using Storage = uintptr_t;

  static constexpr size_t kBitsPerStorage = std::numeric_limits<Storage>::digits;

  // The kControlBits most significant bits of control_ select the representation: kSizedStorageBit
  // and kStorageTypeBit, the rest reserved for future use and always zero. The low kSizeBits hold
  // the size.
  static constexpr size_t  kControlBits = 4;
  static constexpr size_t  kSizeBits = kBitsPerStorage - kControlBits;
  static constexpr Storage kSizeMask = (Storage{1} << kSizeBits) - 1;
  static constexpr Storage kSizedStorageBit = Storage{1} << (kBitsPerStorage - 1);
  // Meaningful only with kSizedStorageBit set: 0 -> inline, 1 -> dynamic.
  static constexpr Storage kStorageTypeBit = Storage{1} << (kBitsPerStorage - 2);
  static constexpr Storage kStorageMask = kSizedStorageBit | kStorageTypeBit;
  static constexpr Storage kInlineStorage = kSizedStorageBit;
  static constexpr Storage kDynamicStorage = kSizedStorageBit | kStorageTypeBit;

 public:
  static constexpr size_t kInlineCapacity = kBitsPerStorage;
  static constexpr size_t kMaxCapacity = static_cast<size_t>(kSizeMask);

  DynamicBitSet() noexcept = default;

  DynamicBitSet(const DynamicBitSet& other);
  DynamicBitSet(DynamicBitSet&& other) noexcept;
  DynamicBitSet& operator=(const DynamicBitSet& other);
  DynamicBitSet& operator=(DynamicBitSet&& other) noexcept;

  ~DynamicBitSet();

  bool test(size_t pos) const;
  void set(size_t pos);
  void swap(DynamicBitSet& other) noexcept;

  // Sizes the set to new_size bits, at most kMaxCapacity. A virtual set gets its uniform value in
  // every bit; a bounded set keeps its bits below new_size and gets any new ones unset.
  void resize(size_t new_size);

  // Most significant bit first, as std::bitset::to_string. "<none>" or "<all>" when virtual.
  std::string ToString() const;

  bool any() const noexcept;

  // For the unbounded (virtual) storage returns max possible value.
  size_t size() const noexcept {
    return IsVirtual() ? std::numeric_limits<size_t>::max() : StorageSize();
  }

  static DynamicBitSet All() noexcept {
    DynamicBitSet result;
    result.storage_ = ~Storage{0};
    return result;
  }

 private:
  // The heap allocation of a dynamic set; defined in dynamic_bitset.cc.
  class DynamicStorage;

  // Clears the bits in [from, to); to must not exceed capacity().
  void ClearBits(size_t from, size_t to) noexcept;

  void Destroy() noexcept;

  bool IsVirtual() const noexcept {
    return (control_ & kSizedStorageBit) == 0;
  }

  bool IsInline() const noexcept {
    return (control_ & kStorageMask) == kInlineStorage;
  }

  bool IsDynamic() const noexcept {
    return (control_ & kStorageMask) == kDynamicStorage;
  }

  size_t StorageSize() const noexcept {
    return static_cast<size_t>(control_ & kSizeMask);
  }

  DynamicStorage* Dynamic() const noexcept {
    return reinterpret_cast<DynamicStorage*>(storage_);
  }

  // The number of bits the set can hold without reallocating: 0 for a virtual set.
  size_t capacity() const noexcept;

  // The blocks holding the bits of a bounded set: storage_ itself when inline.
  // const Storage* when called on a const set. Defined in dynamic_bitset.cc, where every caller is.
  template <class Self>
  auto* blocks(this Self& self) noexcept;

  static constexpr size_t RequiredBlocks(size_t num_bits) noexcept {
    return (num_bits + kBitsPerStorage - 1) / kBitsPerStorage;
  }

  static constexpr size_t BlockIndex(size_t pos) noexcept {
    return pos / kBitsPerStorage;
  }

  static constexpr size_t BitIndex(size_t pos) noexcept {
    return pos % kBitsPerStorage;
  }

  static constexpr Storage BitMask(size_t pos) noexcept {
    return Storage{1} << BitIndex(pos);
  }

  Storage control_ = 0;
  Storage storage_ = 0;

  friend class DynamicBitSetTest;
};

} // namespace yb
