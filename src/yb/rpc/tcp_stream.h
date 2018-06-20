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

#ifndef YB_RPC_TCP_STREAM_H
#define YB_RPC_TCP_STREAM_H

#include <ev++.h>

#include "yb/rpc/growable_buffer.h"
#include "yb/rpc/stream.h"

#include "yb/util/net/socket.h"
#include "yb/util/ref_cnt_buffer.h"

namespace yb {
namespace rpc {

class TcpStream : public Stream {
 public:
  TcpStream(
      const Endpoint& remote, Socket socket, GrowableBufferAllocator* allocator, size_t limit);
  ~TcpStream();

  Socket* socket() { return &socket_; }

  std::string ToString() const;

  static const rpc::Protocol* StaticProtocol();
  static StreamFactoryPtr Factory();

 private:
  CHECKED_STATUS Start(bool connect, ev::loop_ref* loop, StreamContext* context) override;
  void Close() override;
  void Shutdown(const Status& status) override;
  void Send(OutboundDataPtr data) override;
  CHECKED_STATUS TryWrite() override;

  bool Idle(std::string* reason_not_idle) override;
  bool IsConnected() override { return connected_; }
  void DumpPB(const DumpRunningRpcsRequestPB& req, RpcConnectionPB* resp) override;

  const Endpoint& Remote() override { return remote_; }
  const Endpoint& Local() override { return local_; }

  const Protocol* GetProtocol() override {
    return StaticProtocol();
  }

  void ParseReceived() override;

  CHECKED_STATUS DoWrite();
  void HandleOutcome(const Status& status, bool enqueue);
  void ClearSending(const Status& status);

  void Handler(ev::io& watcher, int revents); // NOLINT
  CHECKED_STATUS ReadHandler();
  CHECKED_STATUS WriteHandler(bool just_connected);

  Result<bool> Receive();
  // Try to parse received data and process it.
  Result<bool> TryProcessReceived();

  // Updates listening events.
  void UpdateEvents();

  const std::string& LogPrefix() const;

  // The socket we're communicating on.
  Socket socket_;

  // The remote address we're talking from.
  Endpoint local_;

  // The remote address we're talking to.
  const Endpoint remote_;

  StreamContext* context_;

  mutable std::string log_prefix_;

  // Notifies us when our socket is readable or writable.
  ev::io io_;

  // Set to true when the connection is registered on a loop.
  // This is used for a sanity check in the destructor that we are properly
  // un-registered before shutting down.
  bool is_epoll_registered_ = false;

  bool connected_ = false;

  // Data received on this connection that has not been processed yet.
  GrowableBuffer read_buffer_;
  bool read_buffer_full_ = false;

  // sending_* contain bytes and calls we are currently sending to socket
  std::deque<RefCntBuffer> sending_;
  std::deque<OutboundDataPtr> sending_outbound_datas_;
  size_t send_position_ = 0;
  bool waiting_write_ready_ = false;
};

} // namespace rpc
} // namespace yb

#endif // YB_RPC_TCP_STREAM_H
