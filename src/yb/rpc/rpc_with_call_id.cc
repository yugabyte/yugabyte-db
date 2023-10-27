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

#include "yb/rpc/rpc_with_call_id.h"

#include "yb/rpc/connection.h"
#include "yb/rpc/reactor.h"
#include "yb/rpc/rpc_introspection.pb.h"

#include "yb/util/status_format.h"
#include "yb/util/string_util.h"

namespace yb {
namespace rpc {

ConnectionContextWithCallId::ConnectionContextWithCallId() {}

void ConnectionContextWithCallId::DumpPB(const DumpRunningRpcsRequestPB& req,
                                         RpcConnectionPB* resp) {
  for (const auto &entry : calls_being_handled_) {
    InboundCallPtr call_ptr{entry.second.lock()};
    if (call_ptr) {
      call_ptr->DumpPB(req, resp->add_calls_in_flight());
    }
  }
}

std::string ToString(InboundCallWeakPtr weak_ptr) {
  InboundCallPtr call_ptr{weak_ptr.lock()};
  return (call_ptr ? "deleted" : yb::Format("$0", call_ptr.get()));
}

bool ConnectionContextWithCallId::Idle(std::string* reason_not_idle) {
  if (calls_being_handled_.empty()) {
    return true;
  }

  if (reason_not_idle) {
    AppendWithSeparator(
        Format("$0 calls being handled: $1", calls_being_handled_.size(), calls_being_handled_),
        reason_not_idle);
  }

  return false;
}

Status ConnectionContextWithCallId::Store(InboundCallPtr call) {
  uint64_t call_id = ExtractCallId(call.get());
  InboundCallWeakPtr weak_call_ptr(call);
  if (!calls_being_handled_.emplace(call_id, weak_call_ptr).second) {
    LOG(WARNING) << call->connection()->ToString() << ": received call ID " << call_id
                 << " but was already processing this ID! Ignoring";
    return STATUS_FORMAT(NetworkError, "Received duplicate call id: $0", call_id);
  }
  return Status::OK();
}

void ConnectionContextWithCallId::Shutdown(const Status& status) {
}

void ConnectionContextWithCallId::CallProcessed(InboundCall* call) {
  ++processed_call_count_;
  auto id = ExtractCallId(call);
  auto it = calls_being_handled_.find(id);
  InboundCallPtr call_ptr{it == calls_being_handled_.end() ? nullptr : it->second.lock()};
  if (!call_ptr || call_ptr.get() != call) {
    std::string existing = it == calls_being_handled_.end() || !call_ptr ? "<NONE>" : call_ptr->ToString();
    LOG(DFATAL) << "Processed call with invalid id: " << id << ", call: " << call->ToString()
                << ", existing: " << existing;
    return;
  }
  calls_being_handled_.erase(it);
  if (Idle() && idle_listener_) {
    idle_listener_();
  }
}

Status ConnectionContextWithCallId::QueueResponse(
    const ConnectionPtr& conn, InboundCallPtr call) {
  return conn->QueueOutboundData(std::move(call));
}

} // namespace rpc
} // namespace yb
