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
#include "yb/ash/rpc_wait_state.h"

#include "yb/rpc/inbound_call.h"
#include "yb/rpc/lightweight_message.h"

#include "yb/util/trace.h"

DECLARE_bool(ysql_yb_enable_ash);

namespace yb::ash {

namespace {

// An InboundCall's trace may have multiple sub-traces. It will be nice
// for all the wait-state updates to be on the inbound call's trace rather
// than be spread across the different traces
class WaitStateInfoWithInboundCall : public WaitStateInfo {
 public:
  WaitStateInfoWithInboundCall() = default;
  explicit WaitStateInfoWithInboundCall(int rpc_request_id)
      : WaitStateInfo(rpc_request_id) {}

  void VTrace(int level, GStringPiece data) override {
    std::shared_ptr<const rpc::InboundCall> sptr;
    {
      std::lock_guard guard(mutex_);
      sptr = holder_.lock();
    }
    VTraceTo(sptr ? sptr->trace() : nullptr, level, data);
  }

  void UseForTracing(std::shared_ptr<const rpc::InboundCall> call) {
    std::lock_guard guard(mutex_);
    holder_ = std::move(call);
  }

  std::string DumpTraceToString() override {
    std::shared_ptr<const rpc::InboundCall> sptr;
    {
      std::lock_guard guard(mutex_);
      sptr = holder_.lock();
    }
    return sptr && sptr->trace() ? sptr->trace()->DumpToString(true) : "n/a";
  }

 private:
  simple_spinlock mutex_;
  std::weak_ptr<const rpc::InboundCall> holder_ GUARDED_BY(mutex_);
};

using Output = google::protobuf::io::CodedOutputStream;

} // namespace

CallStateListener::CallStateListener(int64_t instance_id)
    : wait_state_(WaitStateInfo::CreateIfAshIsEnabled<WaitStateInfoWithInboundCall>(instance_id)) {
}

void CallStateListener::UpdateInfo(bool is_local_call, const std::string& method_name) {
  if (!wait_state_) {
    LOG_IF(DFATAL, FLAGS_ysql_yb_enable_ash)
        << "Wait state is nullptr while updating metadata";
    return;
  }

  if (is_local_call) {
    if (const auto& wait_state = ash::WaitStateInfo::CurrentWaitState()) {
      auto metadata = wait_state->metadata();
      metadata.clear_rpc_request_id();
      wait_state_->UpdateMetadata(metadata);
    }
  } else {
    wait_state_->UpdateMetadataFromPB(metadata_);
  }

  wait_state_->UpdateAuxInfo({ .method = method_name });
}

void CallStateListener::UseForTracing(const rpc::InboundCall* call) {
  down_cast<WaitStateInfoWithInboundCall*>(wait_state_.get())->UseForTracing(shared_from(call));
}

const ash::WaitStateInfoPtr& CallStateListener::wait_state() {
  return wait_state_;
}

rpc::AnyMessagePtr CallStateListener::mutable_message() {
  return rpc::AnyMessagePtr(&metadata_);
}

MetadataSerializer::MetadataSerializer(rpc::MetadataSerializationMode mode)
  : mode_(mode) {
  if (const auto& wait_state = ash::WaitStateInfo::CurrentWaitState()) {
    const auto metadata = wait_state->metadata();
    metadata.ToPB(&metadata_);
    serialized_size_ = metadata_.ByteSizeLong();
  }
}

size_t MetadataSerializer::SerializedSize() {
  if (mode_ == rpc::MetadataSerializationMode::kSkipOnZero && serialized_size_ == 0) {
    return 0;
  }
  return Output::VarintSize32(static_cast<uint32_t>(serialized_size_)) + serialized_size_;
}

uint8_t* MetadataSerializer::SerializeToArray(uint8_t* out) {
  using google::protobuf::internal::WireFormatLite;

  DCHECK(mode_ != rpc::MetadataSerializationMode::kSkipOnZero || serialized_size_ != 0)
      << "A zero size is invalid when the mode is kSkipOnZero";

  out = Output::WriteVarint32ToArray(static_cast<uint32_t>(serialized_size_), out);

  if (mode_ == rpc::MetadataSerializationMode::kWriteOnZero && serialized_size_ == 0) {
    return out;
  }

  return metadata_.SerializeWithCachedSizesToArray(out);
}

} // namespace yb::ash
