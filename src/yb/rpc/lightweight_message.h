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

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/wire_format_lite.h>

#include "yb/gutil/casts.h"

#include "yb/rpc/serialization.h"

#include "yb/util/memory/arena.h"
#include "yb/util/memory/arena_list.h"
#include "yb/util/status.h"

namespace yb::rpc {

class LightweightMessage {
 public:
  virtual ~LightweightMessage() = default;

  virtual Status ParseFromCodedStream(google::protobuf::io::CodedInputStream* cis) = 0;
  virtual size_t SerializedSize() const = 0;
  virtual void AppendToDebugString(std::string* out) const = 0;
  virtual void Clear() = 0;

  Status ParseFromSlice(const Slice& slice);

  size_t SpaceUsedLong() const {
    return SerializedSize(); // TODO(LW)
  }

  size_t DynamicMemoryUsage() const {
    return SerializedSize();
  }

  size_t ObjectSize() const {
    return sizeof(*this); // TODO(LW)
  }

  uint8_t* SerializeToArray(uint8_t* out) const {
    return DoSerializeToArray(out);
  }

  std::string ShortDebugString() const;
  std::string SerializeAsString() const;
  void AppendToString(std::string* out) const;
  std::byte* SerializeToArray(std::byte* out) const;

 private:
  virtual uint8_t* DoSerializeToArray(uint8_t* out) const = 0;
};

template <class MsgPtr, class LWMsgPtr>
class AnyMessagePtrBase {
 public:
  AnyMessagePtrBase() : message_(0) {}
  AnyMessagePtrBase(std::nullptr_t) : message_(0) {} // NOLINT

  explicit AnyMessagePtrBase(MsgPtr message) : message_(reinterpret_cast<size_t>(message)) {}
  explicit AnyMessagePtrBase(LWMsgPtr message)
      : message_(message ? reinterpret_cast<size_t>(message) | 1 : 0) {
  }

  bool is_lightweight() const {
    return message_ & 1;
  }

  LWMsgPtr lightweight() const {
    DCHECK(is_lightweight());
    return reinterpret_cast<LWMsgPtr>(message_ ^ 1);
  }

  MsgPtr protobuf() const {
    DCHECK(!is_lightweight());
    return reinterpret_cast<MsgPtr>(message_);
  }

  size_t impl() const {
    return message_;
  }

  std::string ToString() const {
    if (!message_) {
      return AsString(nullptr);
    }
    if (is_lightweight()) {
      return AsString(*lightweight());
    }
    return AsString(*protobuf());
  }

  bool operator!() const {
    return message_ == 0;
  }

 protected:
  friend bool operator==(AnyMessagePtrBase lhs, std::nullptr_t) {
    return lhs.message_ == 0;
  }

  explicit AnyMessagePtrBase(size_t message) : message_(message) {}

  size_t message_;
};

class AnyMessagePtr : public AnyMessagePtrBase<google::protobuf::Message*, LightweightMessage*> {
 public:
  template <class... Args>
  explicit AnyMessagePtr(Args&&... args) : AnyMessagePtrBase(std::forward<Args>(args)...) {}

  void operator=(std::nullptr_t) {
    message_ = 0;
  }

  Status ParseFromSlice(const Slice& slice);
};

class AnyMessageConstPtr : public AnyMessagePtrBase<
    const google::protobuf::Message*, const LightweightMessage*> {
 public:
  template <class... Args>
  explicit AnyMessageConstPtr(Args&&... args) : AnyMessagePtrBase(std::forward<Args>(args)...) {}

  AnyMessageConstPtr(const AnyMessagePtr& rhs) // NOLINT
      : AnyMessagePtrBase(rhs.impl()) {
  }

  size_t SerializedSize() const;

  Result<uint8_t*> SerializeToArray(uint8_t* out) const;
};

template <google::protobuf::internal::WireFormatLite::FieldType type, class T>
class LightweightSerialization {
 public:
  static bool Read(google::protobuf::io::CodedInputStream* input, T* t);
  static uint8_t* Write(T value, uint8_t* out);
  static size_t Size(T value);
};

template <class T>
class LightweightSerialization<google::protobuf::internal::WireFormatLite::TYPE_ENUM, T> {
 public:
  using Impl = LightweightSerialization<
      google::protobuf::internal::WireFormatLite::TYPE_UINT32, uint32_t>;

  static bool Read(google::protobuf::io::CodedInputStream* input, T* t) {
    uint32_t temp;
    if (!Impl::Read(input, &temp)) {
      return false;
    }
    *t = static_cast<T>(temp);
    return true;
  }

  static uint8_t* Write(T value, uint8_t* out) {
    return Impl::Write(value, out);
  }

  static size_t Size(T value) {
    return Impl::Size(value);
  }
};

template <class T>
class LightweightSerialization<google::protobuf::internal::WireFormatLite::TYPE_MESSAGE, T> {
 public:
  using Impl = LightweightSerialization<
      google::protobuf::internal::WireFormatLite::TYPE_UINT32, uint32_t>;

  static bool Read(google::protobuf::io::CodedInputStream* input, T* t) {
    int length;
    if (!input->ReadVarintSizeAsInt(&length)) {
      return false;
    }
    auto p = input->IncrementRecursionDepthAndPushLimit(length);
    if (p.second < 0) {
      return false;
    }
    auto parse_status = t->ParseFromCodedStream(input);
    if (!parse_status.ok()) {
      LOG(WARNING) << "Parse failed: " << parse_status;
      return false;
    }
    return input->DecrementRecursionDepthAndPopLimit(p.first);
  }

  static uint8_t* Write(const T& value, uint8_t* out) {
    auto size = value.cached_size();
    out = google::protobuf::io::CodedOutputStream::WriteVarint32ToArray(
        narrow_cast<uint32_t>(size), out);
    auto start = out;
    auto result = value.SerializeToArray(out);
    LOG_IF(DFATAL, make_unsigned(result - start) != size)
        << "Wrong serialized size: " << result - start << ", expected: " << size;
    return result;
  }

  static size_t Size(const T& value) {
    size_t size = value.SerializedSize();
    return google::protobuf::io::CodedOutputStream::VarintSize32(narrow_cast<uint32_t>(size))
           + size;
  }
};

Status ParseFailed(const char* field_name);

template <class Serialization, size_t TagSize, class Value>
inline size_t RepeatedSize(const Value& value) {
  size_t result = TagSize * value.size();
  for (const auto& entry : value) {
    result += Serialization::Size(entry);
  }
  return result;
}

template <class Serialization, size_t TagSize, class Value>
inline size_t SingleSize(const Value& value) {
  return TagSize + Serialization::Size(value);
}

template <class Serialization, uint32_t Tag, class Value>
inline uint8_t* SingleWrite(const Value& value, uint8_t* out) {
  out = google::protobuf::io::CodedOutputStream::WriteTagToArray(Tag, out);
  return Serialization::Write(value, out);
}

template <class Serialization, uint32_t Tag, class Value>
inline uint8_t* RepeatedWrite(const Value& value, uint8_t* out) {
  for (const auto& entry : value) {
    out = SingleWrite<Serialization, Tag>(entry, out);
  }
  return out;
}

template <class Serialization, size_t TagSize, class Value>
inline size_t PackedSize(const Value& value, size_t* out_body_size) {
  size_t body_size = 0;
  for (const auto& entry : value) {
    body_size += Serialization::Size(entry);
  }
  *out_body_size = body_size;
  return TagSize
         + google::protobuf::io::CodedOutputStream::VarintSize32(narrow_cast<uint32_t>(body_size))
         + body_size;
}

template <class Serialization, uint32_t Tag, class Value>
inline uint8_t* PackedWrite(const Value& value, size_t body_size, uint8_t* out) {
  out = google::protobuf::io::CodedOutputStream::WriteTagToArray(Tag, out);
  out = google::protobuf::io::CodedOutputStream::WriteVarint32ToArray(
      narrow_cast<uint32_t>(body_size), out);
  for (const auto& entry : value) {
    out = Serialization::Write(entry, out);
  }
  return out;
}

ThreadSafeArena& empty_arena();

template <class T>
const T& empty_message() {
  static T result(&empty_arena());
  return result;
}

template <class T, class... Args>
std::shared_ptr<T> SharedMessage(Args&&... args) {
  auto arena = SharedThreadSafeArena();
  auto* t = arena->NewArenaObject<T>(std::forward<Args>(args)...);
  return std::shared_ptr<T>(std::move(arena), t);
}

template <class T>
std::shared_ptr<T> MakeSharedMessage() {
  return SharedMessage<T>();
}

template <class PB>
inline constexpr bool IsGoogleProtobuf = std::is_base_of_v<google::protobuf::Message, PB>;

template <class PB>
inline constexpr bool IsLightweightMessage = std::is_base_of_v<rpc::LightweightMessage, PB>;

template <class LW>
std::enable_if_t<IsLightweightMessage<LW>, LW*> LightweightMessageType(LW*);

template <class PB>
auto CopySharedMessage(const PB& rhs) {
  using LW = typename std::remove_pointer<
      decltype(LightweightMessageType(static_cast<PB*>(nullptr)))>::type;
  return SharedMessage<LW>(rhs);
}

template <class T>
class AsSharedMessageHelper {
 public:
  explicit AsSharedMessageHelper(const T& t) : t_(t) {}

  template <class U>
  operator std::shared_ptr<U>() const {
    return CopySharedMessage<U>(t_);
  }

 private:
  const T& t_;
};

template <class T>
auto AsSharedMessage(const T& t) {
  return AsSharedMessageHelper<T>(t);
}

void AppendFieldTitle(const char* name, const char* suffix, bool* first, std::string* out);

void SetupLimit(google::protobuf::io::CodedInputStream* in);

template <class T>
auto ToRepeatedPtrField(const ArenaList<T>& list) {
  google::protobuf::RepeatedPtrField<decltype(list.front().ToGoogleProtobuf())> result;
  list.ToGoogleProtobuf(&result);
  return result;
}

namespace map_util {

template <class Key, class Value>
const Key& ExtractKey(const google::protobuf::MapPair<Key, Value>& p) {
  return p.first;
}

template <class Key, class Value>
const Value& ExtractValue(const google::protobuf::MapPair<Key, Value>& p) {
  return p.second;
}

} // namespace map_util

} // namespace yb::rpc
