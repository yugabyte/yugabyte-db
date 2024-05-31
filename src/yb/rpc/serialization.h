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

#pragma once

#include <inttypes.h>
#include <string.h>

#include <string>

#include <google/protobuf/message.h>
#include <google/protobuf/repeated_field.h>

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/result.h"
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

Result<RefCntBuffer> SerializeRequest(
    size_t body_size, size_t additional_size, const google::protobuf::Message& header,
    AnyMessageConstPtr body);

size_t SerializedMessageSize(size_t body_size, size_t additional_size);

Status SerializeMessage(
    AnyMessageConstPtr msg, size_t body_size, const RefCntBuffer& param_buf,
    size_t additional_size, size_t offset);

struct ParsedRequestHeader {
  Slice remote_method;
  int32_t call_id = 0;
  uint32_t timeout_ms = 0;
  boost::iterator_range<const uint32_t*> sidecar_offsets;

  std::string RemoteMethodAsString() const;
  void ToPB(RequestHeader* out) const;
};

// Deserialize the request.
// In: data buffer Slice.
// Out: parsed_header PB initialized,
//      parsed_main_message pointing to offset in original buffer containing
//      the main payload.
Status ParseYBMessage(
    const CallData& call_data, ResponseHeader* header, Slice* serialized_pb,
    ReceivedSidecars* sidecars);

Status ParseYBMessage(
    const CallData& call_data, ParsedRequestHeader* header, Slice* serialized_pb,
    ReceivedSidecars* sidecars);

struct ParsedRemoteMethod {
  Slice service;
  Slice method;
};

Result<ParsedRemoteMethod> ParseRemoteMethod(const Slice& buf);

}  // namespace rpc
}  // namespace yb
