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

#ifndef YB_RPC_ACCEPTOR_H
#define YB_RPC_ACCEPTOR_H

#include <mutex>
#include <vector>
#include <unordered_map>

#include <ev++.h> // NOLINT

#include "yb/gutil/atomicops.h"
#include "yb/util/thread.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/net/socket.h"
#include "yb/util/status.h"

namespace yb {

class Counter;
class Socket;

namespace rpc {

class Messenger;

// A acceptor that calls accept() to create new connections.
class Acceptor {
 public:
  // Create a new acceptor pool.
  explicit Acceptor(Messenger *messenger);
  ~Acceptor();

  // 'socket' must be already bound, but should not yet be listening.
  CHECKED_STATUS Add(Socket socket);

  CHECKED_STATUS Start();
  void Shutdown();

 private:
  void RunThread();
  void IoHandler(ev::io& io, int events); // NOLINT
  void AsyncHandler(ev::async& async, int events); // NOLINT

  struct AcceptingSocket {
    std::unique_ptr<ev::io> io;
    Socket socket;
    Sockaddr address;
  };

  Messenger *messenger_;
  scoped_refptr<yb::Thread> thread_;
  std::mutex mutex_;
  std::unordered_map<ev::io*, AcceptingSocket> sockets_;

  std::vector<Socket> sockets_to_add_;
  std::vector<Socket> processing_sockets_to_add_;

  scoped_refptr<Counter> rpc_connections_accepted_;

  bool closing_ = false;
  ev::dynamic_loop loop_;
  ev::async async_;

  DISALLOW_COPY_AND_ASSIGN(Acceptor);
};

} // namespace rpc
} // namespace yb
#endif // YB_RPC_ACCEPTOR_H
