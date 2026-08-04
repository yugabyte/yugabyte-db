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

#include "yb/ash/wait_state.h"

#include "yb/common/common.pb.h"

#include "yb/rpc/wait_state_if.h"

namespace yb::ash {

class CallStateListener : public rpc::CallStateListener {
 public:
  explicit CallStateListener(int64_t instance_id);
  void UpdateInfo(bool is_local_call, const std::string& method_name) override;
  void UseForTracing(const rpc::InboundCall* call) override;
  const ash::WaitStateInfoPtr& wait_state() override;
  rpc::AnyMessagePtr mutable_message() override;

 private:
  AshMetadataPB metadata_;
  const ash::WaitStateInfoPtr wait_state_;
};

class CallStateListenerFactory : public rpc::CallStateListenerFactory {
 public:
  std::unique_ptr<rpc::CallStateListener> Create(int64_t instance_id) override {
    return std::make_unique<CallStateListener>(instance_id);
  }
};

class MetadataSerializer : public rpc::MetadataSerializer {
 public:
  explicit MetadataSerializer(rpc::MetadataSerializationMode mode);
  size_t SerializedSize() override;
  uint8_t* SerializeToArray(uint8_t* out) override;

 private:
  AshMetadataPB metadata_;
  size_t serialized_size_ = 0;
  const rpc::MetadataSerializationMode mode_;
};

class MetadataSerializerFactory : public rpc::MetadataSerializerFactory {
 public:
  std::unique_ptr<rpc::MetadataSerializer> Create(rpc::MetadataSerializationMode mode) override {
    return std::make_unique<ash::MetadataSerializer>(mode);
  }
};

} // namespace yb::ash
