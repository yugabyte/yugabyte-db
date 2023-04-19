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

#include "yb/consensus/replicate_msgs_holder.h"

namespace yb {
namespace consensus {

ReplicateMsgsHolder::ReplicateMsgsHolder(
    google::protobuf::RepeatedPtrField<ReplicateMsg>* ops, ReplicateMsgs messages,
    ScopedTrackedConsumption consumption)
    : ops_(ops), messages_(std::move(messages)), consumption_(std::move(consumption)) {
}

ReplicateMsgsHolder::ReplicateMsgsHolder(ReplicateMsgsHolder&& rhs)
    : ops_(rhs.ops_), messages_(std::move(rhs.messages_)),
      consumption_(std::move(rhs.consumption_)) {
  rhs.ops_ = nullptr;
}

void ReplicateMsgsHolder::operator=(ReplicateMsgsHolder&& rhs) {
  Reset();
  ops_ = rhs.ops_;
  messages_ = std::move(rhs.messages_);
  consumption_ = std::move(rhs.consumption_);
  rhs.ops_ = nullptr;
}

ReplicateMsgsHolder::~ReplicateMsgsHolder() {
  Reset();
}

void ReplicateMsgsHolder::Reset() {
  if (ops_) {
    ops_->ExtractSubrange(0, ops_->size(), nullptr /* elements */);
    ops_ = nullptr;
  }

  messages_.clear();
  consumption_ = ScopedTrackedConsumption();
}

LWReplicateMsgsHolder::LWReplicateMsgsHolder(
    ReplicateMsgs messages, ScopedTrackedConsumption consumption)
    : messages_(std::move(messages)),
      consumption_(std::move(consumption)) {
}

LWReplicateMsgsHolder::LWReplicateMsgsHolder(LWReplicateMsgsHolder&& rhs)
    : messages_(std::move(rhs.messages_)),
      consumption_(std::move(rhs.consumption_)) {
}

void LWReplicateMsgsHolder::operator=(LWReplicateMsgsHolder&& rhs) {
  Reset();
  messages_ = std::move(rhs.messages_);
  consumption_ = std::move(rhs.consumption_);
}

void LWReplicateMsgsHolder::Reset() {
  messages_.clear();
  consumption_ = ScopedTrackedConsumption();
}

}  // namespace consensus
}  // namespace yb
