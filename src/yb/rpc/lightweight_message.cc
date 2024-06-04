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

#include "yb/rpc/lightweight_message.h"

#include <google/protobuf/message.h>

#include "yb/util/pb_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/flags.h"

using namespace yb::size_literals;

// Maximum size of RPC should be larger than size of consensus batch
// At each layer, we embed the "message" from the previous layer.
// In order to send three strings of 64, the request from cql/redis will be larger
// than that because we will have overheads from that layer.
// Hence, we have a limit of 254MB at the consensus layer.
// The rpc layer adds its own headers, so we limit the rpc message size to 255MB.
DEFINE_UNKNOWN_uint64(rpc_max_message_size, 255_MB,
    "The maximum size of a message of any RPC that the server will accept. The sum of "
    "consensus_max_batch_size_bytes and 1KB should be less than rpc_max_message_size");

using google::protobuf::internal::WireFormatLite;
using google::protobuf::io::CodedOutputStream;

namespace yb {
namespace rpc {

namespace {

inline bool SliceRead(google::protobuf::io::CodedInputStream* input, Slice* out) {
  uint32_t length;
  if (!input->ReadVarint32(&length)) {
    return false;
  }
  const void* data;
  int size;
  input->GetDirectBufferPointerInline(&data, &size);
  if (!input->Skip(length)) {
    return false;
  }
  *out = Slice(static_cast<const char*>(data), length);
  return true;
}

inline size_t SliceSize(Slice value) {
  uint32_t size = narrow_cast<uint32_t>(value.size());
  return CodedOutputStream::VarintSize32(size) + size;
}

inline uint8_t* SliceWrite(Slice value, uint8_t* out) {
  uint32_t size = narrow_cast<uint32_t>(value.size());
  out = CodedOutputStream::WriteVarint32ToArray(size, out);
  memcpy(out, value.data(), size);
  return out + size;
}

} // namespace

Status LightweightMessage::ParseFromSlice(const Slice& slice) {
  google::protobuf::io::CodedInputStream in(slice.data(), narrow_cast<int>(slice.size()));
  SetupLimit(&in);
  return ParseFromCodedStream(&in);
}

std::string LightweightMessage::SerializeAsString() const {
  size_t size = SerializedSize();
  std::string result;
  result.resize(size);
  SerializeToArray(pointer_cast<uint8_t*>(const_cast<char*>(result.data())));
  return result;
}

void LightweightMessage::AppendToString(std::string* out) const {
  auto size = SerializedSize();
  auto old_size = out->size();
  out->resize(old_size + size);
  SerializeToArray(pointer_cast<uint8_t*>(out->data()) + old_size);
}

std::string LightweightMessage::ShortDebugString() const {
  std::string result;
  AppendToDebugString(&result);
  return result;
}

Status AnyMessagePtr::ParseFromSlice(const Slice& slice) {
  if (is_lightweight()) {
    return lightweight()->ParseFromSlice(slice);
  }

  google::protobuf::io::CodedInputStream in(slice.data(), narrow_cast<int>(slice.size()));
  SetupLimit(&in);
  auto* proto = protobuf();
  if (PREDICT_FALSE(!proto->ParseFromCodedStream(&in))) {
    return STATUS(InvalidArgument, proto->InitializationErrorString());
  }
  return Status::OK();
}

size_t AnyMessageConstPtr::SerializedSize() const {
  return is_lightweight() ? lightweight()->SerializedSize() : protobuf()->ByteSizeLong();
}

Result<uint8_t*> AnyMessageConstPtr::SerializeToArray(uint8_t* out) const {
  if (is_lightweight()) {
    return lightweight()->SerializeToArray(out);
  }

  auto* proto = protobuf();
  if (PREDICT_FALSE(!proto->IsInitialized())) {
    return STATUS_FORMAT(InvalidArgument, "RPC argument missing required fields: $0 ($1)",
                         proto->InitializationErrorString(), proto->ShortDebugString());
  }

  return proto->SerializeWithCachedSizesToArray(out);
}

template <>
bool LightweightSerialization<WireFormatLite::TYPE_DOUBLE, double>::Read(
    google::protobuf::io::CodedInputStream* input, double* out) {
  return input->ReadLittleEndian64(reinterpret_cast<uint64_t*>(out));
}

template <>
uint8_t* LightweightSerialization<WireFormatLite::TYPE_DOUBLE, double>::Write(
    double value, uint8_t* out) {
  return CodedOutputStream::WriteLittleEndian64ToArray(*reinterpret_cast<uint64*>(&value), out);
}

template <>
bool LightweightSerialization<WireFormatLite::TYPE_FLOAT, float>::Read(
    google::protobuf::io::CodedInputStream* input, float* out) {
  return input->ReadLittleEndian32(reinterpret_cast<uint32_t*>(out));
}

template <>
uint8_t* LightweightSerialization<WireFormatLite::TYPE_FLOAT, float>::Write(
    float value, uint8_t* out) {
  return CodedOutputStream::WriteLittleEndian32ToArray(*reinterpret_cast<uint32*>(&value), out);
}

template <>
bool LightweightSerialization<WireFormatLite::TYPE_INT64, int64_t>::Read(
    google::protobuf::io::CodedInputStream* input, int64_t* out) {
  return input->ReadVarint64(reinterpret_cast<uint64_t*>(out));
}

template <>
uint8_t* LightweightSerialization<WireFormatLite::TYPE_INT64, int64_t>::Write(
    int64_t value, uint8_t* out) {
  return CodedOutputStream::WriteVarint64ToArray(value, out);
}

template <>
size_t LightweightSerialization<WireFormatLite::TYPE_INT64, int64_t>::Size(int64_t value) {
  return CodedOutputStream::VarintSize64(value);
}

template <>
bool LightweightSerialization<WireFormatLite::TYPE_UINT64, uint64_t>::Read(
    google::protobuf::io::CodedInputStream* input, uint64_t* out) {
  return input->ReadVarint64(out);
}

template <>
uint8_t* LightweightSerialization<WireFormatLite::TYPE_UINT64, uint64_t>::Write(
    uint64_t value, uint8_t* out) {
  return CodedOutputStream::WriteVarint64ToArray(value, out);
}

template <>
size_t LightweightSerialization<WireFormatLite::TYPE_UINT64, uint64_t>::Size(uint64_t value) {
  return CodedOutputStream::VarintSize64(value);
}

template <>
bool LightweightSerialization<WireFormatLite::TYPE_INT32, int32_t>::Read(
    google::protobuf::io::CodedInputStream* input, int32_t* out) {
  return input->ReadVarint32(reinterpret_cast<uint32_t*>(out));
}

template <>
uint8_t* LightweightSerialization<WireFormatLite::TYPE_INT32, int32_t>::Write(
    int32_t value, uint8_t* out) {
  return CodedOutputStream::WriteVarint32ToArray(value, out);
}

template <>
size_t LightweightSerialization<WireFormatLite::TYPE_INT32, int32_t>::Size(int32_t value) {
  return CodedOutputStream::VarintSize32(value);
}

template <>
bool LightweightSerialization<WireFormatLite::TYPE_FIXED64, uint64_t>::Read(
    google::protobuf::io::CodedInputStream* input, uint64_t* out) {
  return input->ReadLittleEndian64(out);
}

template <>
uint8_t* LightweightSerialization<WireFormatLite::TYPE_FIXED64, uint64_t>::Write(
    uint64_t value, uint8_t* out) {
  return CodedOutputStream::WriteLittleEndian64ToArray(value, out);
}

template <>
bool LightweightSerialization<WireFormatLite::TYPE_FIXED32, uint32_t>::Read(
    google::protobuf::io::CodedInputStream* input, uint32_t* out) {
  return input->ReadLittleEndian32(out);
}

template <>
uint8_t* LightweightSerialization<WireFormatLite::TYPE_FIXED32, uint32_t>::Write(
    uint32_t value, uint8_t* out) {
  return CodedOutputStream::WriteLittleEndian32ToArray(value, out);
}

template <>
bool LightweightSerialization<WireFormatLite::TYPE_BOOL, bool>::Read(
    google::protobuf::io::CodedInputStream* input, bool* out) {
  uint64 temp;
  if (!input->ReadVarint64(&temp)) {
    return false;
  }
  *out = temp != 0;
  return true;
}

template <>
uint8_t* LightweightSerialization<WireFormatLite::TYPE_BOOL, bool>::Write(
    bool value, uint8_t* out) {
  return CodedOutputStream::WriteVarint64ToArray(value, out);
}

template <>
bool LightweightSerialization<WireFormatLite::TYPE_UINT32, uint32_t>::Read(
    google::protobuf::io::CodedInputStream* input, uint32_t* out) {
  return input->ReadVarint32(out);
}

template <>
uint8_t* LightweightSerialization<WireFormatLite::TYPE_UINT32, uint32_t>::Write(
    uint32_t value, uint8_t* out) {
  return CodedOutputStream::WriteVarint32ToArray(value, out);
}

template <>
size_t LightweightSerialization<WireFormatLite::TYPE_UINT32, uint32_t>::Size(uint32_t value) {
  return CodedOutputStream::VarintSize32(value);
}

template <>
bool LightweightSerialization<WireFormatLite::TYPE_BYTES, Slice>::Read(
    google::protobuf::io::CodedInputStream* input, Slice* out) {
  return SliceRead(input, out);
}

template <>
uint8_t* LightweightSerialization<WireFormatLite::TYPE_BYTES, Slice>::Write(
    Slice value, uint8_t* out) {
  return SliceWrite(value, out);
}

template <>
size_t LightweightSerialization<WireFormatLite::TYPE_BYTES, Slice>::Size(Slice value) {
  return SliceSize(value);
}

template <>
bool LightweightSerialization<WireFormatLite::TYPE_STRING, Slice>::Read(
    google::protobuf::io::CodedInputStream* input, Slice* out) {
  return SliceRead(input, out);
}

template <>
uint8_t* LightweightSerialization<WireFormatLite::TYPE_STRING, Slice>::Write(
    Slice value, uint8_t* out) {
  return SliceWrite(value, out);
}

template <>
size_t LightweightSerialization<WireFormatLite::TYPE_STRING, Slice>::Size(
    Slice value) {
  return SliceSize(value);
}

template <>
bool LightweightSerialization<WireFormatLite::TYPE_SFIXED32, int32_t>::Read(
    google::protobuf::io::CodedInputStream* input, int32_t* out) {
  return input->ReadLittleEndian32(reinterpret_cast<uint32_t*>(out));
}

template <>
uint8_t* LightweightSerialization<WireFormatLite::TYPE_SFIXED32, int32_t>::Write(
    int32_t value, uint8_t* out) {
  return CodedOutputStream::WriteLittleEndian32ToArray(value, out);
}

template <>
bool LightweightSerialization<WireFormatLite::TYPE_SFIXED64, int64_t>::Read(
    google::protobuf::io::CodedInputStream* input, int64_t* out) {
  return input->ReadLittleEndian64(reinterpret_cast<uint64_t*>(out));
}

template <>
uint8_t* LightweightSerialization<WireFormatLite::TYPE_SFIXED64, int64_t>::Write(
    int64_t value, uint8_t* out) {
  return CodedOutputStream::WriteLittleEndian64ToArray(value, out);
}

template <>
bool LightweightSerialization<WireFormatLite::TYPE_SINT32, int32_t>::Read(
    google::protobuf::io::CodedInputStream* input, int32_t* out) {
  uint32_t temp;
  if (!input->ReadVarint32(&temp)) {
    return false;
  }
  *out = WireFormatLite::ZigZagDecode32(temp);
  return true;
}

template <>
uint8_t* LightweightSerialization<WireFormatLite::TYPE_SINT32, int32_t>::Write(
    int32_t value, uint8_t* out) {
  return CodedOutputStream::WriteVarint32ToArray(WireFormatLite::ZigZagEncode32(value), out);
}

template <>
size_t LightweightSerialization<WireFormatLite::TYPE_SINT32, int32_t>::Size(int32_t value) {
  return CodedOutputStream::VarintSize32(WireFormatLite::ZigZagEncode32(value));
}

template <>
bool LightweightSerialization<WireFormatLite::TYPE_SINT64, int64_t>::Read(
    google::protobuf::io::CodedInputStream* input, int64_t* out) {
  uint64_t temp;
  if (!input->ReadVarint64(&temp)) {
    return false;
  }
  *out = WireFormatLite::ZigZagDecode64(temp);
  return true;
}

template <>
uint8_t* LightweightSerialization<WireFormatLite::TYPE_SINT64, int64_t>::Write(
    int64_t value, uint8_t* out) {
  return CodedOutputStream::WriteVarint64ToArray(WireFormatLite::ZigZagEncode64(value), out);
}

template <>
size_t LightweightSerialization<WireFormatLite::TYPE_SINT64, int64_t>::Size(int64_t value) {
  return CodedOutputStream::VarintSize64(WireFormatLite::ZigZagEncode64(value));
}

void AppendFieldTitle(const char* name, const char* suffix, bool* first, std::string* out) {
  if (*first) {
    *first = false;
  } else {
    *out += ' ';
  }
  *out += name;
  *out += suffix;
}

Status ParseFailed(const char* field_name) {
  return STATUS_FORMAT(Corruption, "Failed to parse '$0'", field_name);
}

void SetupLimit(google::protobuf::io::CodedInputStream* in) {
  in->SetTotalBytesLimit(narrow_cast<int>(FLAGS_rpc_max_message_size),
                         narrow_cast<int>(FLAGS_rpc_max_message_size * 3 / 4));
}

ThreadSafeArena& empty_arena() {
  static ThreadSafeArena arena(static_cast<size_t>(0), 0);
  return arena;
}

} // namespace rpc
} // namespace yb
