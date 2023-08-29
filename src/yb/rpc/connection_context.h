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

#ifndef YB_RPC_CONNECTION_CONTEXT_H
#define YB_RPC_CONNECTION_CONTEXT_H

#include <ev++.h>

#include "yb/rpc/rpc_fwd.h"
#include "yb/rpc/rpc_introspection.pb.h"
#include "yb/rpc/reactor_thread_role.h"

#include "yb/util/net/socket.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {

class MemTracker;

namespace rpc {

typedef std::function<void()> IdleListener;

struct ProcessCallsResult {
  size_t consumed = 0;
  Slice buffer;
  size_t bytes_to_skip = 0;

  std::string ToString() const;
};

// ConnectionContext class is used by connection for doing protocol
// specific logic.
class ConnectionContext {
 public:
  virtual ~ConnectionContext() {}

  // Split data into separate calls and invoke them.
  // Returns number of processed bytes.
  virtual Result<ProcessCallsResult> ProcessCalls(
      const ConnectionPtr& connection, const IoVecs& data, ReadBufferFull read_buffer_full) = 0;

  // Dump information about status of this connection context to protobuf.
  virtual void DumpPB(const DumpRunningRpcsRequestPB& req, RpcConnectionPB* resp) = 0;

  // Checks whether this connection context is idle.
  // If reason is supplied, then human-readable description of why the context is not idle is
  // appended to it.
  virtual bool Idle(std::string* reason_not_idle = nullptr) ON_REACTOR_THREAD = 0;

  // Listen for when context becomes idle.
  virtual void ListenIdle(IdleListener listener) = 0;

  // Shutdown this context.
  virtual void Shutdown(const Status& status) ON_REACTOR_THREAD = 0;

  virtual Status QueueResponse(const ConnectionPtr& connection, InboundCallPtr call) = 0;

  virtual void SetEventLoop(ev::loop_ref* loop) {}

  virtual Status AssignConnection(const ConnectionPtr& connection) { return Status::OK(); }

  virtual void Connected(const ConnectionPtr& connection) = 0;

  virtual uint64_t ProcessedCallCount() = 0;

  virtual RpcConnectionPB::StateType State() = 0;

  virtual StreamReadBuffer& ReadBuffer() = 0;

  virtual Status ReportPendingWriteBytes(size_t bytes_in_queue) = 0;

  virtual void UpdateLastRead(const ConnectionPtr& connection);

  virtual void UpdateLastWrite(const ConnectionPtr& connection) {}
};

class ConnectionContextBase : public ConnectionContext {
 public:
  Status ReportPendingWriteBytes(size_t bytes_in_queue) override;
};

class ConnectionContextFactory {
 public:
  ConnectionContextFactory(
      int64_t memory_limit, const std::string& name,
      const std::shared_ptr<MemTracker>& parent_mem_tracker);

  virtual std::unique_ptr<ConnectionContext> Create(size_t receive_buffer_size) = 0;

  const std::shared_ptr<MemTracker>& parent_tracker() {
    return parent_tracker_;
  }

  const std::shared_ptr<MemTracker>& buffer_tracker() {
    return buffer_tracker_;
  }

 protected:
  ~ConnectionContextFactory();

  std::shared_ptr<MemTracker> parent_tracker_;
  std::shared_ptr<MemTracker> call_tracker_;
  std::shared_ptr<MemTracker> buffer_tracker_;
};

template <class ContextType>
class ConnectionContextFactoryImpl : public ConnectionContextFactory {
 public:
  ConnectionContextFactoryImpl(
      int64_t memory_limit = 0,
      const std::shared_ptr<MemTracker>& parent_mem_tracker = nullptr)
      : ConnectionContextFactory(
          memory_limit, ContextType::Name(), parent_mem_tracker) {}

  std::unique_ptr<ConnectionContext> Create(size_t receive_buffer_size) override {
    return std::make_unique<ContextType>(receive_buffer_size, buffer_tracker_, call_tracker_);
  }

  virtual ~ConnectionContextFactoryImpl() {}
};

template <class ContextType, class... Args>
std::shared_ptr<ConnectionContextFactory> CreateConnectionContextFactory(Args&&... args) {
  return std::make_shared<ConnectionContextFactoryImpl<ContextType>>(std::forward<Args>(args)...);
}

} // namespace rpc
} // namespace yb

#endif // YB_RPC_CONNECTION_CONTEXT_H
