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

#include "yb/rpc/acceptor.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>

#include <atomic>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "yb/util/logging.h"
#include <gtest/gtest_prod.h>

#include "yb/gutil/ref_counted.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/rpc/reactor.h"

#include "yb/util/flags.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/thread.h"

METRIC_DEFINE_counter(server, rpc_connections_accepted,
                      "RPC Connections Accepted",
                      yb::MetricUnit::kConnections,
                      "Number of incoming TCP connections made to the RPC server");

DEFINE_UNKNOWN_int32(rpc_acceptor_listen_backlog, 128,
             "Socket backlog parameter used when listening for RPC connections. "
             "This defines the maximum length to which the queue of pending "
             "TCP connections inbound to the RPC server may grow. If a connection "
             "request arrives when the queue is full, the client may receive "
             "an error. Higher values may help the server ride over bursts of "
             "new inbound connection requests.");
TAG_FLAG(rpc_acceptor_listen_backlog, advanced);

namespace yb {
namespace rpc {

Acceptor::Acceptor(const scoped_refptr<MetricEntity>& metric_entity, NewSocketHandler handler)
    : handler_(std::move(handler)),
      rpc_connections_accepted_(METRIC_rpc_connections_accepted.Instantiate(metric_entity)),
      loop_(kDefaultLibEvFlags) {
}

Acceptor::~Acceptor() {
  Shutdown();
}

Status Acceptor::Listen(const Endpoint& endpoint, Endpoint* bound_endpoint) {
  Socket socket;
  RETURN_NOT_OK(socket.Init(endpoint.address().is_v6() ? Socket::FLAG_IPV6 : 0));
  RETURN_NOT_OK(socket.SetReuseAddr(true));
  RETURN_NOT_OK(socket.Bind(endpoint));
  if (bound_endpoint) {
    RETURN_NOT_OK(socket.GetSocketAddress(bound_endpoint));
  }
  RETURN_NOT_OK(socket.SetNonBlocking(true));
  RETURN_NOT_OK(socket.Listen(FLAGS_rpc_acceptor_listen_backlog));

  bool was_empty;
  {
    std::lock_guard lock(mutex_);
    if (closing_) {
      return STATUS_SUBSTITUTE(ServiceUnavailable, "Acceptor closing");
    }
    was_empty = sockets_to_add_.empty();
    sockets_to_add_.push_back(std::move(socket));
  }

  if (was_empty) {
    async_.send();
  }

  return Status::OK();
}

Status Acceptor::Start() {
  async_.set(loop_);
  async_.set<Acceptor, &Acceptor::AsyncHandler>(this);
  async_.start();
  async_.send();
  return yb::Thread::Create("acceptor", "acceptor", &Acceptor::RunThread, this, &thread_);
}

void Acceptor::Shutdown() {
  {
    std::lock_guard lock(mutex_);
    if (closing_) {
      CHECK(sockets_to_add_.empty());
      VLOG(2) << "Acceptor already shut down";
      return;
    }
    closing_ = true;
  }

  if (thread_) {
    async_.send();

    CHECK_OK(ThreadJoiner(thread_.get()).Join());
    thread_.reset();
  }
}

void Acceptor::IoHandler(ev::io& io, int events) {
  auto it = sockets_.find(&io);
  if (it == sockets_.end()) {
    LOG(ERROR) << "IoHandler for unknown socket: " << &io;
    return;
  }
  Socket& socket = it->second.socket;
  if (events & EV_ERROR) {
    LOG(INFO) << "Acceptor socket failure: " << socket.GetFd()
              << ", endpoint: " << it->second.endpoint;
    sockets_.erase(it);
    return;
  }

  if (events & EV_READ) {
    for (;;) {
      Socket new_sock;
      Endpoint remote;
      VLOG(2) << "calling accept() on socket " << socket.GetFd();
      Status s = socket.Accept(&new_sock, &remote, Socket::FLAG_NONBLOCKING);
      if (!s.ok()) {
        if (!s.IsTryAgain()) {
          LOG(WARNING) << "Acceptor: accept failed: " << s;
        }
        return;
      }
      s = new_sock.SetNoDelay(true);
      if (!s.ok()) {
        LOG(WARNING) << "Acceptor with remote = " << remote
                     << " failed to set TCP_NODELAY on a newly accepted socket: "
                     << s.ToString();
        continue;
      }
      rpc_connections_accepted_->Increment();
      handler_(&new_sock, remote);
    }
  }
}

void Acceptor::AsyncHandler(ev::async& async, int events) {
  bool closing;
  {
    std::lock_guard lock(mutex_);
    closing = closing_;
    sockets_to_add_.swap(processing_sockets_to_add_);
  }

  if (closing) {
    processing_sockets_to_add_.clear();
    sockets_.clear();
    loop_.break_loop();
    return;
  }

  while (!processing_sockets_to_add_.empty()) {
    auto& socket = processing_sockets_to_add_.back();
    Endpoint endpoint;
    auto status = socket.GetSocketAddress(&endpoint);
    if (!status.ok()) {
      LOG(WARNING) << "Failed to get address for socket: "
                   << socket.GetFd() << ": " << status.ToString();
    }
    VLOG(1) << "Adding socket fd " << socket.GetFd() << " at " << endpoint;
    AcceptingSocket ac{ std::unique_ptr<ev::io>(new ev::io),
                        Socket(std::move(socket)),
                        endpoint };
    processing_sockets_to_add_.pop_back();
    ac.io->set(loop_);
    ac.io->set<Acceptor, &Acceptor::IoHandler>(this);
    ac.io->start(ac.socket.GetFd(), EV_READ);
    sockets_.emplace(ac.io.get(), std::move(ac));
  }
}

void Acceptor::RunThread() {
  loop_.run();
  VLOG(1) << "Acceptor shutting down.";
}

} // namespace rpc
} // namespace yb
