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

#ifndef YB_RPC_STREAM_H
#define YB_RPC_STREAM_H

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/net/net_fwd.h"
#include "yb/util/net/socket.h"
#include "yb/util/result.h"
#include "yb/util/status.h"

namespace ev {

struct loop_ref;

}

namespace yb {
namespace rpc {

class StreamContext {
 public:
  virtual void UpdateLastActivity() = 0;
  virtual void Transferred(const OutboundDataPtr& data, const Status& status) = 0;
  virtual void Destroy(const Status& status) = 0;
  virtual void Connected() = 0;
  virtual Result<size_t> ProcessReceived(const IoVecs& data, ReadBufferFull read_buffer_full) = 0;

 protected:
  ~StreamContext() {}
};

class Stream {
 public:
  virtual CHECKED_STATUS Start(bool connect, ev::loop_ref* loop, StreamContext* context) = 0;
  virtual void Close() = 0;
  virtual void Shutdown(const Status& status) = 0;
  virtual void Send(OutboundDataPtr data) = 0;
  virtual CHECKED_STATUS TryWrite() = 0;
  virtual void ParseReceived() = 0;

  virtual bool Idle(std::string* reason_not_idle) = 0;
  virtual bool IsConnected() = 0;
  virtual void DumpPB(const DumpRunningRpcsRequestPB& req, RpcConnectionPB* resp) = 0;

  // The address of the remote end of the connection.
  virtual const Endpoint& Remote() = 0;

  // The address of the local end of the connection.
  virtual const Endpoint& Local() = 0;

  virtual const Protocol* GetProtocol() = 0;

  virtual ~Stream() {}
};

class StreamFactory {
 public:
  virtual std::unique_ptr<Stream> Create(
      const Endpoint& remote, Socket socket, GrowableBufferAllocator* allocator, size_t limit) = 0;

  virtual ~StreamFactory() {}
};

class Protocol {
 public:
  explicit Protocol(const std::string& id) : id_(id) {}

  Protocol(const Protocol& schema) = delete;
  void operator=(const Protocol& schema) = delete;

  const std::string& ToString() const { return id_; }

  const std::string& id() const { return id_; }

 private:
  std::string id_;
};

} // namespace rpc
} // namespace yb

#endif // YB_RPC_STREAM_H
