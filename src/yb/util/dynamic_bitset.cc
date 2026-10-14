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

#include "yb/util/dynamic_bitset.h"

#include <algorithm>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "yb/util/logging.h"

namespace yb {

// =======================================
// DynamicBitSet representation invariants
// =======================================
//
// control_:
//   - The kControlBits (four) most significant bits are reserved for representation control,
//     the low kSizeBits hold the size of a bounded set.
//   - The highest bit (kSizedStorageBit): 0 -> virtual (unbounded), 1 -> bounded.
//   - The second-highest bit (kStorageTypeBit), meaningful only when bounded:
//     0 -> inline, 1 -> dynamic.
//   - The two bits under kStorageMask hold one of three states: virtual (both clear),
//     kInlineStorage or kDynamicStorage. kStorageTypeBit alone is invalid.
//   - The other two control bits are reserved for future use and always zero.
//
// Virtual (unbounded):
//   - control_ == 0.
//   - storage_ == Storage{0} is all-unset (<none>): the default and the moved-from state.
//   - storage_ == ~Storage{0} is all-set (<all>), made by All(). No other storage_ value is valid.
//   - size() returns SIZE_MAX. capacity() is 0: no bits are stored.
//   - test(pos) is valid for every pos and returns the uniform value.
//   - set(pos) is invalid (CHECK) until resize() bounds the set.
//   - resize(n) bounds the set with every bit in [0, n) holding the uniform value.
//
// Bounded inline:
//   - control_ & kStorageMask == kInlineStorage.
//   - size() <= kInlineCapacity <= kMaxCapacity, and storage_ holds the bits.
//   - The capacity is kInlineCapacity.
//
// Bounded dynamic:
//   - control_ & kStorageMask == kDynamicStorage.
//   - storage_ is a DynamicStorage*: the number of blocks, followed by the bit blocks in one
//     allocation. The number of blocks is fixed at allocation and at most
//     RequiredBlocks(kMaxCapacity).
//   - The capacity is DynamicStorage::capacity(), and size() <= capacity.
//   - A set becomes dynamic only by growing past kInlineCapacity, and never goes back to inline.
//     Shrinking keeps the allocation, so size() may later be at or below kInlineCapacity.
//   - Growing past the capacity reallocates to exactly RequiredBlocks(size()) blocks.
//
// Every bounded set:
//   - test(pos) and set(pos) require pos < size() (CHECK).
//   - Every bit in [size(), capacity()) is zero. resize() of a virtual set clears them past the
//     new size; shrinking clears the dropped bits. That is why growing within the capacity only
//     updates the size, and why any() needs no popcount.
//   - resize() is the only operation that changes the size or the representation.
//
// When extending:
//   - An operation that clears bits or decreases size() must keep the bits in
//     [size(), capacity()) zero, e.g. with ClearBits().
//   - A shrink_to_fit() would be the place to free unused blocks or to go back to inline storage.

static_assert(sizeof(DynamicBitSet) == 2 * sizeof(uintptr_t));

// The heap allocation of a dynamic set: the number of blocks, followed by the blocks themselves.
class DynamicBitSet::DynamicStorage {
 public:
  size_t num_blocks() const noexcept {
    return num_blocks_;
  }

  size_t capacity() const noexcept {
    return num_blocks_ * kBitsPerStorage;
  }

  // The blocks right after this object: const Storage* when called on a const DynamicStorage.
  template <class Self>
  auto* blocks(this Self& self) noexcept {
    using Block = std::conditional_t<std::is_const_v<Self>, const Storage, Storage>;
    return reinterpret_cast<Block*>(&self + 1);
  }

  // Blocks at or above the set's size() are zero, so all of them can be scanned.
  bool any() const noexcept {
    // TODO: Could be O(1) by keeping a count of the set bits and comparing it with zero,
    // at the cost of maintaining it in set() and resize().
    return std::any_of(blocks(), blocks() + num_blocks(), [](Storage block) {
      return block != 0;
    });
  }

  // Creates storage for num_blocks blocks, each set to value.
  static DynamicStorage* Create(size_t num_blocks, Storage value) {
    auto* result = Allocate(num_blocks);
    std::fill_n(result->blocks(), num_blocks, value);
    return result;
  }

  // Creates storage for num_blocks blocks: a copy of the first of the num_source_blocks blocks
  // at source_blocks, followed by zero blocks if num_blocks is larger.
  static DynamicStorage* Create(
      size_t num_blocks, const Storage* source_blocks, size_t num_source_blocks) {
    auto* result = Allocate(num_blocks);
    const auto num_copied = std::min(num_blocks, num_source_blocks);
    std::copy_n(source_blocks, num_copied, result->blocks());
    std::fill_n(result->blocks() + num_copied, num_blocks - num_copied, Storage{0});
    return result;
  }

  static DynamicStorage* Clone(const DynamicStorage& other) {
    return Create(other.num_blocks(), other.blocks(), other.num_blocks());
  }

  static void Destroy(DynamicStorage* storage) noexcept {
    std::destroy_at(storage);
    ::operator delete(storage);
  }

 private:
  explicit DynamicStorage(size_t num_blocks) noexcept : num_blocks_(num_blocks) {}

  // Allocates storage for num_blocks blocks, left uninitialized.
  static DynamicStorage* Allocate(size_t num_blocks) {
    // The blocks follow num_blocks_ without padding.
    static_assert(sizeof(DynamicStorage) == sizeof(Storage));
    static_assert(alignof(DynamicStorage) == alignof(Storage));
    CHECK_LE(num_blocks, RequiredBlocks(kMaxCapacity))
        << "DynamicBitSet storage exceeds kMaxCapacity bits";

    void* memory = ::operator new(sizeof(DynamicStorage) + num_blocks * sizeof(Storage));
    return new (memory) DynamicStorage(num_blocks);
  }

  const size_t num_blocks_;
};

size_t DynamicBitSet::capacity() const noexcept {
  if (IsInline()) {
    return kInlineCapacity;
  }
  return IsDynamic() ? Dynamic()->capacity() : 0;
}

template <class Self>
auto* DynamicBitSet::blocks(this Self& self) noexcept {
  return self.IsInline() ? &self.storage_ : self.Dynamic()->blocks();
}

DynamicBitSet::DynamicBitSet(const DynamicBitSet& other)
    : control_(other.control_), storage_(other.storage_) {
  if (IsDynamic()) {
    storage_ = reinterpret_cast<Storage>(DynamicStorage::Clone(*other.Dynamic()));
  }
}

DynamicBitSet::DynamicBitSet(DynamicBitSet&& other) noexcept
    : control_(std::exchange(other.control_, 0)),
      storage_(std::exchange(other.storage_, 0)) {
}

DynamicBitSet& DynamicBitSet::operator=(const DynamicBitSet& other) {
  if (this != &other) {
    DynamicBitSet copy(other);
    swap(copy);
  }
  return *this;
}

DynamicBitSet& DynamicBitSet::operator=(DynamicBitSet&& other) noexcept {
  if (this != &other) {
    Destroy();
    control_ = std::exchange(other.control_, 0);
    storage_ = std::exchange(other.storage_, 0);
  }
  return *this;
}

DynamicBitSet::~DynamicBitSet() {
  Destroy();
}

void DynamicBitSet::swap(DynamicBitSet& other) noexcept {
  std::swap(control_, other.control_);
  std::swap(storage_, other.storage_);
}

std::string DynamicBitSet::ToString() const {
  if (IsVirtual()) {
    return storage_ ? "<all>" : "<none>";
  }

  const auto size = StorageSize();
  std::string result;
  result.reserve(size);
  for (auto pos = size; pos != 0; --pos) {
    result.push_back(test(pos - 1) ? '1' : '0');
  }
  return result;
}

bool DynamicBitSet::any() const noexcept {
  return IsDynamic() ? Dynamic()->any() : storage_ != 0;
}

bool DynamicBitSet::test(size_t pos) const {
  if (IsVirtual()) {
    return storage_ != 0;
  }
  CHECK_LT(pos, StorageSize());
  return blocks()[BlockIndex(pos)] & BitMask(pos);
}

void DynamicBitSet::set(size_t pos) {
  CHECK(!IsVirtual()) << "resize() a DynamicBitSet before setting its bits";
  CHECK_LT(pos, StorageSize());
  blocks()[BlockIndex(pos)] |= BitMask(pos);
}

void DynamicBitSet::resize(size_t new_size) {
  CHECK_LE(new_size, kMaxCapacity) << "DynamicBitSet size exceeds kMaxCapacity";

  if (IsVirtual()) {
    if (new_size <= kInlineCapacity) {
      control_ = kInlineStorage | static_cast<Storage>(new_size);
    } else {
      // storage_ still holds the uniform value of the virtual set.
      storage_ = reinterpret_cast<Storage>(
          DynamicStorage::Create(RequiredBlocks(new_size), storage_));
      control_ = kDynamicStorage | static_cast<Storage>(new_size);
    }
    ClearBits(new_size, capacity());
    return;
  }

  const auto size = StorageSize();
  if (new_size < size) {
    ClearBits(new_size, size);
  } else if (new_size > capacity()) {
    // Inline bits are one block. Every allocated block is copied: those past size() are zero.
    auto* new_storage = DynamicStorage::Create(
        RequiredBlocks(new_size), blocks(), IsInline() ? 1 : Dynamic()->num_blocks());
    Destroy();
    storage_ = reinterpret_cast<Storage>(new_storage);
    control_ = kDynamicStorage;
  }
  control_ = (control_ & kStorageMask) | static_cast<Storage>(new_size);
}

void DynamicBitSet::ClearBits(size_t from, size_t to) noexcept {
  if (from >= to) {
    return;
  }
  auto* data = blocks();
  const auto first = BlockIndex(from);
  // Keeps the bits of the first block below from; BitMask(from) - 1 is 0 when from starts a block.
  data[first] &= BitMask(from) - 1;
  std::fill(data + first + 1, data + RequiredBlocks(to), Storage{0});
}

void DynamicBitSet::Destroy() noexcept {
  if (IsDynamic()) {
    DynamicStorage::Destroy(Dynamic());
  }
}

} // namespace yb
