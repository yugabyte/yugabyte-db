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

#ifndef KUDU_RPC_SERIALIZATION_H
#define KUDU_RPC_SERIALIZATION_H

#include <inttypes.h>
#include <string.h>

namespace google {
namespace protobuf {
class MessageLite;
} // namespace protobuf
} // namespace google

namespace kudu {

class Status;
class faststring;
class Slice;

namespace rpc {
namespace serialization {

// Serialize the request param into a buffer which is allocated by this function.
// Uses the message's cached size by calling MessageLite::GetCachedSize().
// In : 'message' Protobuf Message to serialize
//      'additional_size' Optional argument which increases the recorded size
//        within param_buf. This argument is necessary if there will be
//        additional sidecars appended onto the message (that aren't part of
//        the protobuf itself).
//      'use_cached_size' Additional optional argument whether to use the cached
//        or explicit byte size by calling MessageLite::GetCachedSize() or
//        MessageLite::ByteSize(), respectively.
// Out: The faststring 'param_buf' to be populated with the serialized bytes.
//        The faststring's length is only determined by the amount that
//        needs to be serialized for the protobuf (i.e., no additional space
//        is reserved for 'additional_size', which only affects the
//        size indicator prefix in 'param_buf').
Status SerializeMessage(const google::protobuf::MessageLite& message,
                        faststring* param_buf, int additional_size = 0,
                        bool use_cached_size = false);

// Serialize the request or response header into a buffer which is allocated
// by this function.
// Includes leading 32-bit length of the buffer.
// In: Protobuf Header to serialize,
//     Length of the message param following this header in the frame.
// Out: faststring to be populated with the serialized bytes.
Status SerializeHeader(const google::protobuf::MessageLite& header,
                       size_t param_len,
                       faststring* header_buf);

// Deserialize the request.
// In: data buffer Slice.
// Out: parsed_header PB initialized,
//      parsed_main_message pointing to offset in original buffer containing
//      the main payload.
Status ParseMessage(const Slice& buf,
                    google::protobuf::MessageLite* parsed_header,
                    Slice* parsed_main_message);

// Serialize the RPC connection header (magic number + flags).
// buf must have 7 bytes available (kMagicNumberLength + kHeaderFlagsLength).
void SerializeConnHeader(uint8_t* buf);

// Validate the entire rpc header (magic number + flags).
Status ValidateConnHeader(const Slice& slice);


} // namespace serialization
} // namespace rpc
} // namespace kudu
#endif // KUDU_RPC_SERIALIZATION_H
