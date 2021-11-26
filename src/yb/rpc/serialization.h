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

#ifndef YB_RPC_SERIALIZATION_H_
#define YB_RPC_SERIALIZATION_H_

#include <inttypes.h>
#include <string.h>

#include <string>

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/slice.h"

namespace google {
namespace protobuf {
class MessageLite;
}  // namespace protobuf
}  // namespace google

namespace yb {

class faststring;
class RefCntBuffer;
class Slice;
class Status;

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
                        RefCntBuffer* param_buf,
                        int additional_size = 0,
                        bool use_cached_size = false,
                        size_t offset = 0,
                        size_t* size = nullptr);

// Serialize the request or response header into a buffer which is allocated
// by this function.
// Includes leading 32-bit length of the buffer.
// In: Protobuf Header to serialize,
//     Length of the message param following this header in the frame.
// Out: RefCntBuffer to be populated with the serialized bytes.
Status SerializeHeader(const google::protobuf::MessageLite& header,
                       size_t param_len,
                       RefCntBuffer* header_buf,
                       size_t reserve_for_param = 0,
                       size_t* header_size = nullptr);

struct ParsedRequestHeader {
  Slice remote_method;
  int32_t call_id = 0;
  uint32_t timeout_ms = 0;

  std::string RemoteMethodAsString() const;
  void ToPB(RequestHeader* out) const;
};

// Deserialize the request.
// In: data buffer Slice.
// Out: parsed_header PB initialized,
//      parsed_main_message pointing to offset in original buffer containing
//      the main payload.
CHECKED_STATUS ParseYBMessage(const Slice& buf,
                              google::protobuf::MessageLite* parsed_header,
                              Slice* parsed_main_message);


CHECKED_STATUS ParseYBMessage(const Slice& buf,
                              ParsedRequestHeader* parsed_header,
                              Slice* parsed_main_message);

struct ParsedRemoteMethod {
  Slice service;
  Slice method;
};

Result<ParsedRemoteMethod> ParseRemoteMethod(const Slice& buf);

}  // namespace serialization
}  // namespace rpc
}  // namespace yb

#endif  // YB_RPC_SERIALIZATION_H_
