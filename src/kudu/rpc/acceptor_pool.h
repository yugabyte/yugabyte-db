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

#ifndef KUDU_RPC_ACCEPTOR_POOL_H
#define KUDU_RPC_ACCEPTOR_POOL_H

#include <vector>

#include "kudu/gutil/atomicops.h"
#include "kudu/util/thread.h"
#include "kudu/util/net/sockaddr.h"
#include "kudu/util/net/socket.h"
#include "kudu/util/status.h"

namespace kudu {

class Counter;
class Socket;

namespace rpc {

class Messenger;

// A pool of threads calling accept() to create new connections.
// Acceptor pool threads terminate when they notice that the messenger has been
// shut down, if Shutdown() is called, or if the pool object is destructed.
class AcceptorPool {
 public:
  // Create a new acceptor pool.  Calls socket::Release to take ownership of the
  // socket.
  // 'socket' must be already bound, but should not yet be listening.
  AcceptorPool(Messenger *messenger, Socket *socket, Sockaddr bind_address);
  ~AcceptorPool();

  // Start listening and accepting connections.
  Status Start(int num_threads);
  void Shutdown();

  // Return the address that the pool is bound to. If the port is specified as
  // 0, then this will always return port 0.
  Sockaddr bind_address() const;

  // Return the address that the pool is bound to. This only works while the
  // socket is open, and if the specified port is 0 then this will return the
  // actual port that was bound.
  Status GetBoundAddress(Sockaddr* addr) const;

 private:
  void RunThread();

  Messenger *messenger_;
  Socket socket_;
  Sockaddr bind_address_;
  std::vector<scoped_refptr<kudu::Thread> > threads_;

  scoped_refptr<Counter> rpc_connections_accepted_;

  Atomic32 closing_;

  DISALLOW_COPY_AND_ASSIGN(AcceptorPool);
};

} // namespace rpc
} // namespace kudu
#endif
