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
#ifndef KUDU_UTIL_PROTOBUF_UTIL_H
#define KUDU_UTIL_PROTOBUF_UTIL_H

#include <google/protobuf/message_lite.h>

namespace kudu {

bool AppendPBToString(const google::protobuf::MessageLite &msg, faststring *output) {
  int old_size = output->size();
  int byte_size = msg.ByteSize();
  output->resize(old_size + byte_size);
  uint8* start = reinterpret_cast<uint8*>(output->data() + old_size);
  uint8* end = msg.SerializeWithCachedSizesToArray(start);
  CHECK(end - start == byte_size)
    << "Error in serialization. byte_size=" << byte_size
    << " new ByteSize()=" << msg.ByteSize()
    << " end-start=" << (end-start);
  return true;
}

} // namespace kudu

#endif
