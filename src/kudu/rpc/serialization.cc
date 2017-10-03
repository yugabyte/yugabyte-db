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

#include "kudu/rpc/serialization.h"

#include <glog/logging.h>
#include <google/protobuf/message_lite.h>
#include <google/protobuf/io/coded_stream.h>

#include "kudu/gutil/endian.h"
#include "kudu/gutil/stringprintf.h"
#include "kudu/rpc/constants.h"
#include "kudu/util/faststring.h"
#include "kudu/util/slice.h"
#include "kudu/util/status.h"

DECLARE_int32(rpc_max_message_size);

using google::protobuf::MessageLite;
using google::protobuf::io::CodedInputStream;
using google::protobuf::io::CodedOutputStream;

namespace kudu {
namespace rpc {
namespace serialization {

enum {
  kHeaderPosVersion = 0,
  kHeaderPosServiceClass = 1,
  kHeaderPosAuthProto = 2
};

Status SerializeMessage(const MessageLite& message, faststring* param_buf,
                        int additional_size, bool use_cached_size) {

  if (PREDICT_FALSE(!message.IsInitialized())) {
    return Status::InvalidArgument("RPC argument missing required fields",
        message.InitializationErrorString());
  }
  int pb_size = use_cached_size ? message.GetCachedSize() : message.ByteSize();
  DCHECK_EQ(message.ByteSize(), pb_size);
  int recorded_size = pb_size + additional_size;
  int size_with_delim = pb_size + CodedOutputStream::VarintSize32(recorded_size);
  int total_size = size_with_delim + additional_size;

  if (total_size > FLAGS_rpc_max_message_size) {
    LOG(DFATAL) << "Sending too long of an RPC message (" << total_size
                << " bytes)";
  }

  param_buf->resize(size_with_delim);
  uint8_t* dst = param_buf->data();
  dst = CodedOutputStream::WriteVarint32ToArray(recorded_size, dst);
  dst = message.SerializeWithCachedSizesToArray(dst);
  CHECK_EQ(dst, param_buf->data() + size_with_delim);

  return Status::OK();
}

Status SerializeHeader(const MessageLite& header,
                       size_t param_len,
                       faststring* header_buf) {

  if (PREDICT_FALSE(!header.IsInitialized())) {
    LOG(DFATAL) << "Uninitialized RPC header";
    return Status::InvalidArgument("RPC header missing required fields",
                                  header.InitializationErrorString());
  }

  // Compute all the lengths for the packet.
  size_t header_pb_len = header.ByteSize();
  size_t header_tot_len = kMsgLengthPrefixLength        // Int prefix for the total length.
      + CodedOutputStream::VarintSize32(header_pb_len)  // Varint delimiter for header PB.
      + header_pb_len;                                  // Length for the header PB itself.
  size_t total_size = header_tot_len + param_len;

  header_buf->resize(header_tot_len);
  uint8_t* dst = header_buf->data();

  // 1. The length for the whole request, not including the 4-byte
  // length prefix.
  NetworkByteOrder::Store32(dst, total_size - kMsgLengthPrefixLength);
  dst += sizeof(uint32_t);

  // 2. The varint-prefixed RequestHeader PB
  dst = CodedOutputStream::WriteVarint32ToArray(header_pb_len, dst);
  dst = header.SerializeWithCachedSizesToArray(dst);

  // We should have used the whole buffer we allocated.
  CHECK_EQ(dst, header_buf->data() + header_tot_len);

  return Status::OK();
}

Status ParseMessage(const Slice& buf,
                    MessageLite* parsed_header,
                    Slice* parsed_main_message) {

  // First grab the total length
  if (PREDICT_FALSE(buf.size() < kMsgLengthPrefixLength)) {
    return Status::Corruption("Invalid packet: not enough bytes for length header",
                              buf.ToDebugString());
  }

  int total_len = NetworkByteOrder::Load32(buf.data());
  DCHECK_EQ(total_len + kMsgLengthPrefixLength, buf.size())
    << "Got mis-sized buffer: " << buf.ToDebugString();

  CodedInputStream in(buf.data(), buf.size());
  in.Skip(kMsgLengthPrefixLength);

  uint32_t header_len;
  if (PREDICT_FALSE(!in.ReadVarint32(&header_len))) {
    return Status::Corruption("Invalid packet: missing header delimiter",
                              buf.ToDebugString());
  }

  CodedInputStream::Limit l;
  l = in.PushLimit(header_len);
  if (PREDICT_FALSE(!parsed_header->ParseFromCodedStream(&in))) {
    return Status::Corruption("Invalid packet: header too short",
                              buf.ToDebugString());
  }
  in.PopLimit(l);

  uint32_t main_msg_len;
  if (PREDICT_FALSE(!in.ReadVarint32(&main_msg_len))) {
    return Status::Corruption("Invalid packet: missing main msg length",
                              buf.ToDebugString());
  }

  if (PREDICT_FALSE(!in.Skip(main_msg_len))) {
    return Status::Corruption(
        StringPrintf("Invalid packet: data too short, expected %d byte main_msg", main_msg_len),
        buf.ToDebugString());
  }

  if (PREDICT_FALSE(in.BytesUntilLimit() > 0)) {
    return Status::Corruption(
      StringPrintf("Invalid packet: %d extra bytes at end of packet", in.BytesUntilLimit()),
      buf.ToDebugString());
  }

  *parsed_main_message = Slice(buf.data() + buf.size() - main_msg_len,
                              main_msg_len);
  return Status::OK();
}

void SerializeConnHeader(uint8_t* buf) {
  memcpy(reinterpret_cast<char *>(buf), kMagicNumber, kMagicNumberLength);
  buf += kMagicNumberLength;
  buf[kHeaderPosVersion] = kCurrentRpcVersion;
  buf[kHeaderPosServiceClass] = 0; // TODO: implement
  buf[kHeaderPosAuthProto] = 0; // TODO: implement
}

// validate the entire rpc header (magic number + flags)
Status ValidateConnHeader(const Slice& slice) {
  DCHECK_EQ(kMagicNumberLength + kHeaderFlagsLength, slice.size())
    << "Invalid RPC header length";

  // validate actual magic
  if (!slice.starts_with(kMagicNumber)) {
    return Status::InvalidArgument("Connection must begin with magic number", kMagicNumber);
  }

  const uint8_t *data = slice.data();
  data += kMagicNumberLength;

  // validate version
  if (data[kHeaderPosVersion] != kCurrentRpcVersion) {
    return Status::InvalidArgument("Unsupported RPC version",
        StringPrintf("Received: %d, Supported: %d",
            data[kHeaderPosVersion], kCurrentRpcVersion));
  }

  // TODO: validate additional header flags:
  // RPC_SERVICE_CLASS
  // RPC_AUTH_PROTOCOL

  return Status::OK();
}

} // namespace serialization
} // namespace rpc
} // namespace kudu
