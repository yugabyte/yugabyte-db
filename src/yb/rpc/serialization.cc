// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
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

#include "yb/rpc/serialization.h"

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/message.h>

#include "yb/gutil/endian.h"
#include "yb/gutil/stringprintf.h"

#include "yb/rpc/constants.h"
#include "yb/rpc/lightweight_message.h"

#include "yb/rpc/call_data.h"
#include "yb/rpc/rpc_header.pb.h"

#include "yb/util/faststring.h"
#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/result.h"
#include "yb/util/slice.h"
#include "yb/util/status_format.h"

DECLARE_uint64(rpc_max_message_size);

using google::protobuf::MessageLite;
using google::protobuf::io::CodedInputStream;
using google::protobuf::io::CodedOutputStream;

namespace yb {
namespace rpc {

size_t SerializedMessageSize(size_t body_size, size_t additional_size) {
  auto full_size = body_size + additional_size;
  return body_size + CodedOutputStream::VarintSize32(narrow_cast<uint32_t>(full_size));
}

Status SerializeMessage(
    AnyMessageConstPtr msg, size_t body_size, const RefCntBuffer& param_buf,
    size_t additional_size, size_t offset) {
  DCHECK_EQ(msg.SerializedSize(), body_size);
  auto size = SerializedMessageSize(body_size, additional_size);

  auto total_size = size + additional_size;
  if (total_size > FLAGS_rpc_max_message_size) {
    return STATUS_FORMAT(InvalidArgument, "Sending too long RPC message ($0 bytes)", total_size);
  }

  CHECK_EQ(param_buf.size(), offset + size) << "offset = " << offset;
  uint8_t *dst = param_buf.udata() + offset;
  dst = CodedOutputStream::WriteVarint32ToArray(
      narrow_cast<uint32_t>(body_size + additional_size), dst);
  dst = VERIFY_RESULT(msg.SerializeToArray(dst));
  CHECK_EQ(dst - param_buf.udata(), param_buf.size());

  return Status::OK();
}

Status SerializeHeader(const MessageLite& header,
                       size_t param_len,
                       RefCntBuffer* header_buf,
                       size_t reserve_for_param,
                       size_t* header_size) {
  if (PREDICT_FALSE(!header.IsInitialized())) {
    LOG(DFATAL) << "Uninitialized RPC header";
    return STATUS(InvalidArgument, "RPC header missing required fields",
                                  header.InitializationErrorString());
  }

  // Compute all the lengths for the packet.
  size_t header_pb_len = header.ByteSize();
  size_t header_tot_len = kMsgLengthPrefixLength        // Int prefix for the total length.
      + CodedOutputStream::VarintSize32(
            narrow_cast<uint32_t>(header_pb_len))      // Varint delimiter for header PB.
      + header_pb_len;                                  // Length for the header PB itself.
  size_t total_size = header_tot_len + param_len;

  *header_buf = RefCntBuffer(header_tot_len + reserve_for_param);
  if (header_size != nullptr) {
    *header_size = header_tot_len;
  }
  uint8_t* dst = header_buf->udata();

  // 1. The length for the whole request, not including the 4-byte
  // length prefix.
  NetworkByteOrder::Store32(dst, narrow_cast<uint32_t>(total_size - kMsgLengthPrefixLength));
  dst += sizeof(uint32_t);

  // 2. The varint-prefixed RequestHeader PB
  dst = CodedOutputStream::WriteVarint32ToArray(narrow_cast<uint32_t>(header_pb_len), dst);
  dst = header.SerializeWithCachedSizesToArray(dst);

  // We should have used the whole buffer we allocated.
  CHECK_EQ(dst, header_buf->udata() + header_tot_len);

  return Status::OK();
}

Result<RefCntBuffer> SerializeRequest(
    size_t body_size, size_t additional_size, const google::protobuf::Message& header,
    AnyMessageConstPtr body) {
  auto message_size = SerializedMessageSize(body_size, additional_size);
  size_t header_size = 0;
  RefCntBuffer result;
  RETURN_NOT_OK(SerializeHeader(
      header, message_size + additional_size, &result, message_size, &header_size));

  RETURN_NOT_OK(SerializeMessage(body, body_size, result, additional_size, header_size));
  return result;
}

bool SkipField(uint8_t type, CodedInputStream* in) {
  switch (type) {
    case 0: {
      uint64_t temp;
      return in->ReadVarint64(&temp);
    }
    case 1:
      return in->Skip(8);
    case 2: {
      uint32_t temp;
      return in->ReadVarint32(&temp) && in->Skip(temp);
    }
    case 5:
      return in->Skip(4);
    default:
      return false;
  }
}

Result<Slice> ParseString(const Slice& buf, const char* name, CodedInputStream* in) {
  uint32_t len;
  if (!in->ReadVarint32(&len) || in->BytesUntilLimit() < implicit_cast<int>(len)) {
    return STATUS(Corruption, "Unable to decode field", Slice(name));
  }
  Slice result(buf.data() + in->CurrentPosition(), len);
  in->Skip(len);
  return result;
}

Status ParseHeader(
    Slice buf, CodedInputStream* in, ParsedRequestHeader* parsed_header) {
  while (in->BytesUntilLimit() > 0) {
    auto tag = in->ReadTag();
    auto field = tag >> 3;
    switch (field) {
      case RequestHeader::kCallIdFieldNumber: {
        uint32_t temp;
        if (!in->ReadVarint32(&temp)) {
          return STATUS(Corruption, "Unable to decode call_id field");
        }
        parsed_header->call_id = static_cast<int32_t>(temp);
        } break;
      case RequestHeader::kRemoteMethodFieldNumber:
        parsed_header->remote_method = VERIFY_RESULT(ParseString(buf, "remote_method", in));
        break;
      case RequestHeader::kTimeoutMillisFieldNumber:
        if (!in->ReadVarint32(&parsed_header->timeout_ms)) {
          return STATUS(Corruption, "Unable to decode timeout_ms field");
        }
        break;
      case RequestHeader::kSidecarOffsetsFieldNumber: {
          uint32 length;
          if (!in->ReadVarint32(&length)) {
            return STATUS(Corruption, "Unable to decode sidecars field length");
          }
          auto start = pointer_cast<const uint32_t*>(buf.data() + in->CurrentPosition());
          parsed_header->sidecar_offsets = boost::make_iterator_range(
              start, start + length / sizeof(uint32_t));
          in->Skip(length);
        }
        break;
      default: {
        if (!SkipField(tag & 7, in)) {
          return STATUS_FORMAT(Corruption, "Unable to skip: $0", tag);
        }
      }
    }
  }

  return Status::OK();
}

Status ParseHeader(Slice buf, CodedInputStream* in, MessageLite* parsed_header) {
  if (PREDICT_FALSE(!parsed_header->ParseFromCodedStream(in))) {
    return STATUS(Corruption, "Invalid packet: header too short",
                              buf.ToDebugString());
  }

  return Status::OK();
}

namespace {

template <class Header>
Result<Slice> ParseYBHeader(Slice buf, Header* parsed_header) {
  CodedInputStream in(buf.data(), narrow_cast<int>(buf.size()));
  SetupLimit(&in);

  uint32_t header_len;
  if (PREDICT_FALSE(!in.ReadVarint32(&header_len))) {
    return STATUS(Corruption, "Invalid packet: missing header delimiter",
                              buf.ToDebugString());
  }

  auto l = in.PushLimit(header_len);
  RETURN_NOT_OK(ParseHeader(buf, &in, parsed_header));
  in.PopLimit(l);

  uint32_t main_msg_len;
  if (PREDICT_FALSE(!in.ReadVarint32(&main_msg_len))) {
    return STATUS(Corruption, "Invalid packet: missing main msg length",
                              buf.ToDebugString());
  }

  if (PREDICT_FALSE(!in.Skip(main_msg_len))) {
    return STATUS(Corruption,
        StringPrintf("Invalid packet: data too short, expected %d byte main_msg", main_msg_len),
        buf.ToDebugString());
  }

  if (PREDICT_FALSE(in.BytesUntilLimit() > 0)) {
    return STATUS(Corruption,
      StringPrintf("Invalid packet: %d extra bytes at end of packet", in.BytesUntilLimit()),
      buf.ToDebugString());
  }

  return buf.Suffix(main_msg_len);
}

const auto& sidecar_offsets(const ParsedRequestHeader& header) {
  return header.sidecar_offsets;
}

auto sidecar_offsets(const ResponseHeader& header) {
  auto start = header.sidecar_offsets().data();
  return boost::make_iterator_range(start, start + header.sidecar_offsets_size());
}

template <class Header>
Status DoParseYBMessage(
    const CallData& call_data, Header* header, Slice* serialized_pb, ReceivedSidecars* sidecars) {
  auto entire_message = VERIFY_RESULT(ParseYBHeader(call_data.buffer().AsSlice(), header));

  // Use information from header to extract the payload slices.
  auto offsets = sidecar_offsets(*header);

  if (!offsets.empty()) {
    *serialized_pb = entire_message.Prefix(offsets[0]);
    RETURN_NOT_OK(sidecars->Parse(entire_message, offsets));
  } else {
    *serialized_pb = entire_message;
  }
  return Status::OK();
}

} // namespace

Status ParseYBMessage(
    const CallData& call_data, ResponseHeader* header, Slice* serialized_pb,
    ReceivedSidecars* sidecars) {
  return DoParseYBMessage(call_data, header, serialized_pb, sidecars);
}

Status ParseYBMessage(
    const CallData& call_data, ParsedRequestHeader* header, Slice* serialized_pb,
    ReceivedSidecars* sidecars) {
  return DoParseYBMessage(call_data, header, serialized_pb, sidecars);
}

Result<ParsedRemoteMethod> ParseRemoteMethod(const Slice& buf) {
  CodedInputStream in(buf.data(), narrow_cast<int>(buf.size()));
  in.PushLimit(narrow_cast<int>(buf.size()));
  ParsedRemoteMethod result;
  while (in.BytesUntilLimit() > 0) {
    auto tag = in.ReadTag();
    auto field = tag >> 3;
    switch (field) {
      case RemoteMethodPB::kServiceNameFieldNumber:
        result.service = VERIFY_RESULT(ParseString(buf, "service_name", &in));
        break;
      case RemoteMethodPB::kMethodNameFieldNumber:
        result.method = VERIFY_RESULT(ParseString(buf, "method_name", &in));
        break;
      default: {
        if (!SkipField(tag & 7, &in)) {
          return STATUS_FORMAT(Corruption, "Unable to skip: $0", tag);
        }
      }
    }
  }
  return result;
}

std::string ParsedRequestHeader::RemoteMethodAsString() const {
  auto parsed_remote_method = ParseRemoteMethod(remote_method);
  if (parsed_remote_method.ok()) {
    return parsed_remote_method->service.ToBuffer() + "." +
           parsed_remote_method->method.ToBuffer();
  } else {
    return parsed_remote_method.status().ToString();
  }
}

void ParsedRequestHeader::ToPB(RequestHeader* out) const {
  out->set_call_id(call_id);
  if (timeout_ms) {
    out->set_timeout_millis(timeout_ms);
  }
  auto parsed_remote_method = ParseRemoteMethod(remote_method);
  if (parsed_remote_method.ok()) {
    out->mutable_remote_method()->set_service_name(parsed_remote_method->service.ToBuffer());
    out->mutable_remote_method()->set_method_name(parsed_remote_method->method.ToBuffer());
  }
}

}  // namespace rpc
}  // namespace yb
