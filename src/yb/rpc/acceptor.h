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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <mutex>
#include <vector>
#include <unordered_map>

#undef EV_ERROR
#include <ev++.h> // NOLINT

#include "yb/gutil/atomicops.h"
#include "yb/gutil/ref_counted.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/net/socket.h"
#include "yb/util/status_fwd.h"

namespace yb {

class Counter;
class MetricEntity;
class Socket;
class Thread;

namespace rpc {

// Take ownership of the socket via Socket::Release
typedef std::function<void(Socket *new_socket, const Endpoint& remote)> NewSocketHandler;

// A acceptor that calls accept() to create new connections.
class Acceptor {
 public:
  // Create a new acceptor pool.
  Acceptor(const scoped_refptr<MetricEntity>& metric_entity, NewSocketHandler handler);
  ~Acceptor();

  // Setup acceptor to listen address.
  // Return bound address in bound_address.
  Status Listen(const Endpoint& endpoint, Endpoint* bound_endpoint = nullptr);

  Status Start();
  void Shutdown();

 private:
  void RunThread();
  void IoHandler(ev::io& io, int events); // NOLINT
  void AsyncHandler(ev::async& async, int events); // NOLINT

  struct AcceptingSocket {
    std::unique_ptr<ev::io> io;
    Socket socket;
    Endpoint endpoint;
  };

  NewSocketHandler handler_;
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
