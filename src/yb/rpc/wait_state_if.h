//
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
//
#pragma once

#include <memory>

#include "yb/ash/ash_fwd.h"

#include "yb/rpc/rpc_fwd.h"

namespace yb::rpc {

// In case of serialization for shared memory, we need to write the
// size of the metadata, because there are no tags like protobuf.
// For RPCs, we can skip writing the metadata if the size is zero.
YB_DEFINE_ENUM(MetadataSerializationMode, (kSkipOnZero)(kWriteOnZero));

class CallStateListener {
 public:
  virtual ~CallStateListener() = default;
  virtual void UpdateInfo(bool is_local_call, const std::string& method_name) = 0;
  virtual void UseForTracing(const InboundCall* call) = 0;
  virtual const ash::WaitStateInfoPtr& wait_state() = 0;
  virtual AnyMessagePtr mutable_message() = 0;
};

class CallStateListenerFactory {
 public:
  virtual ~CallStateListenerFactory() = default;
  virtual std::unique_ptr<CallStateListener> Create(int64_t instance_id) = 0;
};

class MetadataSerializer {
 public:
  virtual ~MetadataSerializer() = default;
  virtual size_t SerializedSize() = 0;
  virtual uint8_t* SerializeToArray(uint8_t* out) = 0;
};

class MetadataSerializerFactory {
 public:
  virtual ~MetadataSerializerFactory() = default;
  virtual std::unique_ptr<MetadataSerializer> Create(rpc::MetadataSerializationMode mode) = 0;
};

} // namespace yb::rpc
