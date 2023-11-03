//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.


#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "yb/rocksdb/comparator.h"
#include "yb/rocksdb/metadata.h"
#include "yb/rocksdb/slice_transform.h"
#include "yb/rocksdb/status.h"
#include "yb/rocksdb/types.h"
#include "yb/rocksdb/util/coding.h"

#include "yb/util/slice.h"

namespace rocksdb {

class InternalKey;

// Value types encoded as the last component of internal keys.
// DO NOT CHANGE THESE ENUM VALUES: they are embedded in the on-disk
// data structures.
// The highest bit of the value type needs to be reserved to SST tables
// for them to do more flexible encoding.
enum ValueType : unsigned char {
  kTypeDeletion = 0x0,
  kTypeValue = 0x1,
  kTypeMerge = 0x2,
  kTypeLogData = 0x3,               // WAL only.
  kTypeColumnFamilyDeletion = 0x4,  // WAL only.
  kTypeColumnFamilyValue = 0x5,     // WAL only.
  kTypeColumnFamilyMerge = 0x6,     // WAL only.
  kTypeSingleDeletion = 0x7,
  kTypeColumnFamilySingleDeletion = 0x8,  // WAL only.
  kMaxValue = 0x7F                        // Not used for storing records.
};

// kValueTypeForSeek defines the ValueType that should be passed when
// constructing a ParsedInternalKey object for seeking to a particular
// sequence number (since we sort sequence numbers in decreasing order
// and the value type is embedded as the low 8 bits in the sequence
// number in internal keys, we need to use the highest-numbered
// ValueType, not the lowest).
static const ValueType kValueTypeForSeek = kTypeSingleDeletion;

// Checks whether a type is a value type (i.e. a type used in memtables and sst
// files).
inline bool IsValueType(ValueType t) {
  return t <= kTypeMerge || t == kTypeSingleDeletion;
}

// We leave eight bits empty at the bottom so a type and sequence#
// can be packed together into 64-bits.
static const SequenceNumber kMaxSequenceNumber =
    ((0x1ull << 56) - 1);

// Size of the last component of the internal key. Appended by RocksDB, consists of encoded
// ValueType and SequenceNumber.
constexpr auto kLastInternalComponentSize = 8;

struct ParsedInternalKey {
  Slice user_key;
  SequenceNumber sequence;
  ValueType type;

  ParsedInternalKey() { }  // Intentionally left uninitialized (for speed)
  ParsedInternalKey(const Slice& u, const SequenceNumber& seq, ValueType t)
      : user_key(u), sequence(seq), type(t) { }
  std::string DebugString(bool hex = false) const;
};

// Return the length of the encoding of "key".
inline size_t InternalKeyEncodingLength(const ParsedInternalKey& key) {
  return key.user_key.size() + kLastInternalComponentSize;
}

// Pack a sequence number and a ValueType into a uint64_t
extern uint64_t PackSequenceAndType(uint64_t seq, ValueType t);

struct SequenceAndType {
  SequenceNumber sequence;
  ValueType type;
};

SequenceAndType UnPackSequenceAndTypeFromEnd(const void* end);

// Append the serialization of "key" to *result.
extern void AppendInternalKey(std::string* result,
                              const ParsedInternalKey& key);

// Attempt to parse an internal key from "internal_key".  On success,
// stores the parsed data in "*result", and returns true.
//
// On error, returns false, leaves "*result" in an undefined state.
extern bool ParseInternalKey(const Slice& internal_key,
                             ParsedInternalKey* result);

// Returns the user key portion of an internal key.
inline Slice ExtractUserKey(const Slice& internal_key) {
  assert(internal_key.size() >= kLastInternalComponentSize);
  return internal_key.WithoutSuffix(kLastInternalComponentSize);
}

inline ValueType ExtractValueType(const Slice& internal_key) {
  assert(internal_key.size() >= kLastInternalComponentSize);
  const size_t n = internal_key.size();
  uint64_t num = DecodeFixed64(internal_key.data() + n - kLastInternalComponentSize);
  unsigned char c = num & 0xff;
  return static_cast<ValueType>(c);
}

inline size_t RoundUpTo16(const size_t size) {
  return (size + 15) & ~0xF;
}

// A comparator for internal keys that uses a specified comparator for
// the user key portion and breaks ties by decreasing sequence number.
class InternalKeyComparator : public Comparator {
 private:
  const Comparator* user_comparator_;
  std::string name_;
 public:
  explicit InternalKeyComparator(const Comparator* c) : user_comparator_(c),
    name_("rocksdb.InternalKeyComparator:" +
          std::string(user_comparator_->Name())) {
  }
  virtual ~InternalKeyComparator() {}

  virtual const char* Name() const override;
  virtual int Compare(Slice a, Slice b) const override;
  virtual void FindShortestSeparator(std::string* start,
                                     const Slice& limit) const override;
  virtual void FindShortSuccessor(std::string* key) const override;

  const Comparator* user_comparator() const { return user_comparator_; }

  int Compare(const InternalKey& a, const InternalKey& b) const;
  int Compare(const ParsedInternalKey& a, const ParsedInternalKey& b) const;
};

typedef std::shared_ptr<const InternalKeyComparator> InternalKeyComparatorPtr;

// Modules in this directory should keep internal keys wrapped inside
// the following class instead of plain strings so that we do not
// incorrectly use string comparisons instead of an InternalKeyComparator.
class InternalKey {
 private:
  std::string rep_;
 public:
  InternalKey() { }   // Leave rep_ as empty to indicate it is invalid
  InternalKey(const Slice& _user_key, SequenceNumber s, ValueType t) {
    AppendInternalKey(&rep_, ParsedInternalKey(_user_key, s, t));
  }

  // Returns the internal key that is bigger or equal to all internal keys with this user key.
  static InternalKey MaxPossibleForUserKey(const Slice& _user_key) {
    return InternalKey(_user_key, kMaxSequenceNumber, kValueTypeForSeek);
  }

  // Returns the internal key that is smaller or equal to all internal keys with this user key.
  static InternalKey MinPossibleForUserKey(const Slice& _user_key) {
    return InternalKey(_user_key, 0, static_cast<ValueType>(0));
  }

  bool Valid() const {
    ParsedInternalKey parsed;
    return ParseInternalKey(Slice(rep_), &parsed);
  }

  static InternalKey __attribute__ ((warn_unused_result)) DecodeFrom(const Slice& s) {
    return InternalKey(s);
  }

  Slice Encode() const {
    assert(!rep_.empty());
    return rep_;
  }

  Slice user_key() const { return ExtractUserKey(rep_); }
  size_t size() const { return rep_.size(); }
  bool empty() const { return rep_.empty(); }

  void SetFrom(const ParsedInternalKey& p) {
    rep_.clear();
    AppendInternalKey(&rep_, p);
  }

  void Clear() { rep_.clear(); }

  std::string DebugString(bool hex = false) const {
    return DebugString(rep_, hex);
  }

  static std::string DebugString(const std::string& rep, bool hex = false);
 private:
  explicit InternalKey(const Slice& slice)
      : rep_(slice.cdata(), slice.size()) {
  }
};

class BoundaryValuesExtractor {
 public:
  virtual Status Extract(Slice user_key, UserBoundaryValueRefs* values) = 0;
  virtual UserFrontierPtr CreateFrontier() = 0;
 protected:
  ~BoundaryValuesExtractor() {}
};

// Create FileBoundaryValues from specified user_key, seqno, value_type.
inline FileBoundaryValues<InternalKey> MakeFileBoundaryValues(
    const std::string& user_key,
    SequenceNumber seqno,
    ValueType value_type) {
  FileBoundaryValues<InternalKey> result;
  result.key = InternalKey(user_key, seqno, value_type);
  result.seqno = seqno;
  return result;
}

inline int InternalKeyComparator::Compare(
    const InternalKey& a, const InternalKey& b) const {
  return Compare(a.Encode(), b.Encode());
}

inline bool ParseInternalKey(const Slice& internal_key,
                             ParsedInternalKey* result) {
  const size_t n = internal_key.size();
  if (n < kLastInternalComponentSize) return false;
  uint64_t num = DecodeFixed64(internal_key.data() + n - kLastInternalComponentSize);
  unsigned char c = num & 0xff;
  result->sequence = num >> 8;
  result->type = static_cast<ValueType>(c);
  assert(result->type <= ValueType::kMaxValue);
  result->user_key = Slice(internal_key.data(), n - kLastInternalComponentSize);
  return IsValueType(result->type);
}

// Update the sequence number in the internal key.
// Guarantees not to invalidate ikey.data().
inline void UpdateInternalKey(std::string* ikey, uint64_t seq, ValueType t) {
  size_t ikey_sz = ikey->size();
  assert(ikey_sz >= kLastInternalComponentSize);
  uint64_t newval = (seq << 8) | t;

  // Note: Since C++11, strings are guaranteed to be stored contiguously and
  // string::operator[]() is guaranteed not to change ikey.data().
  EncodeFixed64(&(*ikey)[ikey_sz - kLastInternalComponentSize], newval);
}

// Get the sequence number from the internal key
inline uint64_t GetInternalKeySeqno(const Slice& internal_key) {
  const size_t n = internal_key.size();
  assert(n >= kLastInternalComponentSize);
  uint64_t num = DecodeFixed64(internal_key.data() + n - kLastInternalComponentSize);
  return num >> 8;
}


// A helper class useful for DBImpl::Get()
class LookupKey {
 public:
  // Initialize *this for looking up user_key at a snapshot with
  // the specified sequence number.
  LookupKey(const Slice& _user_key, SequenceNumber sequence);

  ~LookupKey();

  // Return a key suitable for lookup in a MemTable.
  Slice memtable_key() const {
    return Slice(start_, static_cast<size_t>(end_ - start_));
  }

  // Return an internal key (suitable for passing to an internal iterator)
  Slice internal_key() const {
    return Slice(kstart_, static_cast<size_t>(end_ - kstart_));
  }

  // Return the user key
  Slice user_key() const {
    return Slice(kstart_, static_cast<size_t>(end_ - kstart_ - kLastInternalComponentSize));
  }

 private:
  // We construct a char array of the form:
  //    klength  varint32               <-- start_
  //    userkey  char[klength]          <-- kstart_
  //    tag      uint64
  //                                    <-- end_
  // The array is a suitable MemTable key.
  // The suffix starting with "userkey" can be used as an InternalKey.
  const char* start_;
  const char* kstart_;
  const char* end_;
  char space_[200];      // Avoid allocation for short keys

  // No copying allowed
  LookupKey(const LookupKey&);
  void operator=(const LookupKey&);
};

inline LookupKey::~LookupKey() {
  if (start_ != space_) delete[] start_;
}

class IterKey {
 public:
  IterKey()
      : buf_(space_), buf_size_(sizeof(space_)), key_(buf_, static_cast<size_t>(0)) {}

  IterKey(const IterKey&) = delete;
  void operator=(const IterKey&) = delete;

  ~IterKey() { ReleaseBuffer(); }

  Slice GetKey() const { return key_; }

  Slice GetUserKey() const {
    assert(key_.size() >= kLastInternalComponentSize);
    return key_.WithoutSuffix(kLastInternalComponentSize);
  }

  inline size_t Size() const { return key_.size(); }

  void Clear() { key_ = Slice(); }

  // Append "non_shared_data" to its back, from "shared_len"
  // This function is used in Block::Iter::ParseNextKey
  // shared_len: bytes in [0, shard_len-1] would be remained
  // non_shared_data: data to be appended, its length must be >= non_shared_len
  void TrimAppend(const size_t shared_len, const char* non_shared_data,
                  const size_t non_shared_len) {
    assert(shared_len <= key_.size());
    size_t total_size = shared_len + non_shared_len;

    if (IsKeyPinned() /* key is not in buf_ */) {
      // Copy the key from external memory to buf_ (copy shared_len bytes)
      EnlargeBufferIfNeeded(total_size);
      memcpy(buf_, key_.data(), shared_len);
    } else if (total_size > buf_size_) {
      // Need to allocate space, delete previous space
      char* p = new char[total_size];
      memcpy(p, key_.data(), shared_len);
      ReleaseBuffer();

      buf_ = p;
      buf_size_ = total_size;
    }

    memcpy(buf_ + shared_len, non_shared_data, non_shared_len);
    key_ = Slice(buf_, total_size);
  }

  // Updates key_ based on passed information about sizes of components to reuse and
  // non_shared_data.
  // key_ contents will be updated to:
  // ----------------------------------------------------------------------
  // |shared_prefix|non_shared_1|shared_middle|non_shared_2|last_component|
  // ----------------------------------------------------------------------
  // where (we use a Python slice like syntax below for byte array slices, end offset is exclusive):
  // shared_prefix is: key_[0:shared_prefix_size]
  // non_shared_1 is: non_shared_data[0:non_shared_1_size]
  // shared_middle is:
  //     key_[source_shared_middle_start:source_shared_middle_start + shared_middle_size]
  // non_shared_2 is:
  //     non_shared_data[non_shared_1_size:non_shared_1_size + non_shared_2_size]
  // last_component is: key_[key_.size() - shared_last_component_size:key_.size()]
  //     increased by shared_last_component_increase (as uint64_t for performance reasons).
  // shared_last_component_size should be either 0 or kLastInternalComponentSize (8).
  //
  // This function might reallocate buf_ that is used to store key_ data if its size is not enough.
  void Update(
      const char* non_shared_data, const uint32_t shared_prefix_size,
      const uint32_t non_shared_1_size, const uint32_t source_shared_middle_start,
      const uint32_t shared_middle_size, const uint32_t non_shared_2_size,
      const uint32_t shared_last_component_size, const uint64_t shared_last_component_increase) {
    DCHECK_LE(
        shared_prefix_size + shared_middle_size + shared_last_component_size, key_.size());

    // Following constants contains offsets of respective updated key component according to the
    // diagram above.
    const auto new_shared_middle_start = shared_prefix_size + non_shared_1_size;
    const auto new_non_shared_2_start = new_shared_middle_start + shared_middle_size;
    const auto new_shared_last_component_start = new_non_shared_2_start + non_shared_2_size;

    const auto new_key_size = new_shared_last_component_start + shared_last_component_size;

    uint64_t last_component FASTDEBUG_FAKE_INIT(0);
    if (shared_last_component_size > 0) {
      // Get shared last component from key_ and increase it by shared_last_component_increase
      // (could be 0 if we just reuse last component as is).
      DCHECK_EQ(shared_last_component_size, kLastInternalComponentSize);
      last_component = DecodeFixed64(key_.cend() - kLastInternalComponentSize) +
                       shared_last_component_increase;
    }

    if (IsKeyPinned() /* key_ pointer was set externally and not owned by us */ ) {
      // Copy shared_prefix and shared_middle to new positions. Since key_ was set externally by
      // KeyIter::SetKey caller, it doesn't overlap with buf_ owned by KeyIter and we can just use
      // memcpy.
      EnlargeBufferIfNeeded(new_key_size);
      memcpy(buf_, key_.data(), shared_prefix_size);
      memcpy(buf_ + new_shared_middle_start, key_.data() + source_shared_middle_start,
             shared_middle_size);
    } else if (new_key_size > buf_size_) {
      // Enlarge buf_ to be enough to store updated key and copy shared_prefix and shared_middle to
      // new positions.
      const auto new_buf_size = RoundUpTo16(new_key_size);
      char* p = new char[new_buf_size];

      memcpy(p, key_.data(), shared_prefix_size);
      memcpy(p + new_shared_middle_start, key_.data() + source_shared_middle_start,
             shared_middle_size);

      ReleaseBuffer();

      buf_ = p;
      buf_size_ = new_buf_size;
    } else {
      // buf_ has enough size to store updated key, no need to reallocate, just move
      // shared_middle to the new position (shared_prefix always starts at the beginning, so it is
      // already there).
      DCHECK_EQ(buf_, key_.cdata());
      if (new_shared_middle_start != source_shared_middle_start && shared_middle_size > 0) {
        memmove(
            buf_ + new_shared_middle_start, key_.data() + source_shared_middle_start,
            shared_middle_size);
      }
    }

    // At this point buf_ contains shared_prefix and shared_middle at new positions for the updated
    // key.
    if (shared_last_component_size > 0) {
      // Put last_component to its new place.
      EncodeFixed64(buf_ + new_shared_last_component_start, last_component);
    }

    // Copy non-shared components.
    memcpy(buf_ + shared_prefix_size, non_shared_data, non_shared_1_size);
    if (non_shared_2_size > 0) {
      memcpy(buf_ + new_non_shared_2_start, non_shared_data + non_shared_1_size, non_shared_2_size);
    }
    key_ = Slice(buf_, new_key_size);
  }

  Slice SetKey(Slice key, bool copy = true) {
    if (!copy) {
      // Update key_ to point to external memory
      key_ = key;
      return key;
    }
    size_t size = key.size();
    // Copy key to buf_
    EnlargeBufferIfNeeded(size);
    key.CopyTo(buf_);
    return key_ = Slice(buf_, size);
  }

  // Copies the content of key, updates the reference to the user key in ikey
  // and returns a Slice referencing the new copy.
  Slice SetKey(Slice key, ParsedInternalKey* ikey) {
    assert(key.size() >= kLastInternalComponentSize);
    auto result = SetKey(key);
    ikey->user_key = result.WithoutSuffix(kLastInternalComponentSize);
    return result;
  }

  // Update the sequence number in the internal key.  Guarantees not to
  // invalidate slices to the key (and the user key).
  void UpdateInternalKey(uint64_t seq, ValueType t) {
    assert(!IsKeyPinned());
    assert(key_.size() >= kLastInternalComponentSize);
    uint64_t newval = (seq << 8) | t;
    EncodeFixed64(buf_ + key_.size() - kLastInternalComponentSize, newval);
  }

  // Returns true iff key_ is not owned by KeyIter, but instead was set externally
  // using KeyIter::SetKey(key, /* copy = */ false).
  bool IsKeyPinned() const { return key_.cdata() != buf_; }

  void SetInternalKey(const Slice& key_prefix, const Slice& user_key,
                      SequenceNumber s,
                      ValueType value_type = kValueTypeForSeek) {
    size_t psize = key_prefix.size();
    size_t usize = user_key.size();
    EnlargeBufferIfNeeded(psize + usize + sizeof(uint64_t));
    if (psize > 0) {
      memcpy(buf_, key_prefix.data(), psize);
    }
    memcpy(buf_ + psize, user_key.data(), usize);
    EncodeFixed64(buf_ + usize + psize, PackSequenceAndType(s, value_type));

    key_ = Slice(buf_, psize + usize + sizeof(uint64_t));
  }

  void SetInternalKey(const Slice& user_key, SequenceNumber s,
                      ValueType value_type = kValueTypeForSeek) {
    SetInternalKey(Slice(), user_key, s, value_type);
  }

  void Reserve(size_t size) {
    EnlargeBufferIfNeeded(size);
    key_ = Slice(buf_, size);
  }

  void SetInternalKey(const ParsedInternalKey& parsed_key) {
    SetInternalKey(Slice(), parsed_key);
  }

  void SetInternalKey(const Slice& key_prefix,
                      const ParsedInternalKey& parsed_key_suffix) {
    SetInternalKey(key_prefix, parsed_key_suffix.user_key,
                   parsed_key_suffix.sequence, parsed_key_suffix.type);
  }

  void EncodeLengthPrefixedKey(const Slice& key) {
    auto size = key.size();
    EnlargeBufferIfNeeded(size + static_cast<size_t>(VarintLength(size)));
    char* ptr = EncodeVarint32(buf_, static_cast<uint32_t>(size));
    memcpy(ptr, key.data(), size);
    key_ = buf_;
  }

 private:
  void ReleaseBuffer() {
    if (buf_ != space_) {
      delete[] buf_;
    }
  }

  // Enlarge the buffer size if needed based on key_size.
  // By default, static allocated buffer is used. Once there is a key
  // larger than the static allocated buffer, another buffer is dynamically
  // allocated, until a larger key buffer is requested. In that case, we
  // reallocate buffer and delete the old one.
  void EnlargeBufferIfNeeded(const size_t key_size) {
    // If size is smaller than buffer size, continue using current buffer,
    // or the static allocated one, as default
    if (key_size <= buf_size_) {
      return;
    }

    // Need to enlarge the buffer.
    ReleaseBuffer();
    const auto new_buf_size = RoundUpTo16(key_size);
    buf_ = new char[new_buf_size];
    buf_size_ = new_buf_size;
  }

  // No copying allowed
  char* buf_;
  size_t buf_size_;
  Slice key_;
  char space_[32];  // Avoid allocation for short keys
};

class InternalKeySliceTransform : public SliceTransform {
 public:
  explicit InternalKeySliceTransform(const SliceTransform* transform)
      : transform_(transform) {}

  virtual const char* Name() const override { return transform_->Name(); }

  virtual Slice Transform(const Slice& src) const override {
    auto user_key = ExtractUserKey(src);
    return transform_->Transform(user_key);
  }

  virtual bool InDomain(const Slice& src) const override {
    auto user_key = ExtractUserKey(src);
    return transform_->InDomain(user_key);
  }

  virtual bool InRange(const Slice& dst) const override {
    auto user_key = ExtractUserKey(dst);
    return transform_->InRange(user_key);
  }

  const SliceTransform* user_prefix_extractor() const { return transform_; }

 private:
  // Like comparator, InternalKeySliceTransform will not take care of the
  // deletion of transform_
  const SliceTransform* const transform_;
};

// Read record from a write batch piece from input.
// tag, column_family, key, value and blob are return values. Callers own the
// Slice they point to.
// Tag is defined as ValueType.
// input will be advanced to after the record.
extern Status ReadRecordFromWriteBatch(Slice* input, char* tag,
                                       uint32_t* column_family, Slice* key,
                                       Slice* value, Slice* blob);
}  // namespace rocksdb
