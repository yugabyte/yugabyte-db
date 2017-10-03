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
#ifndef KUDU_RPC_RPC_H
#define KUDU_RPC_RPC_H

#include <memory>
#include <string>

#include "kudu/gutil/callback.h"
#include "kudu/rpc/rpc_controller.h"
#include "kudu/util/monotime.h"
#include "kudu/util/status_callback.h"

namespace kudu {

namespace rpc {

class Messenger;
class Rpc;

// Provides utilities for retrying failed RPCs.
//
// All RPCs should use HandleResponse() to retry certain generic errors.
class RpcRetrier {
 public:
  RpcRetrier(MonoTime deadline, std::shared_ptr<rpc::Messenger> messenger)
      : attempt_num_(1),
        deadline_(std::move(deadline)),
        messenger_(std::move(messenger)) {
    if (deadline_.Initialized()) {
      controller_.set_deadline(deadline_);
    }
    controller_.Reset();
  }

  // Tries to handle a failed RPC.
  //
  // If it was handled (e.g. scheduled for retry in the future), returns
  // true. In this case, callers should ensure that 'rpc' remains alive.
  //
  // Otherwise, returns false and writes the controller status to
  // 'out_status'.
  bool HandleResponse(Rpc* rpc, Status* out_status);

  // Retries an RPC at some point in the near future. If 'why_status' is not OK,
  // records it as the most recent error causing the RPC to retry. This is
  // reported to the caller eventually if the RPC never succeeds.
  //
  // If the RPC's deadline expires, the callback will fire with a timeout
  // error when the RPC comes up for retrying. This is true even if the
  // deadline has already expired at the time that Retry() was called.
  //
  // Callers should ensure that 'rpc' remains alive.
  void DelayedRetry(Rpc* rpc, const Status& why_status);

  RpcController* mutable_controller() { return &controller_; }
  const RpcController& controller() const { return controller_; }

  const MonoTime& deadline() const { return deadline_; }

  const std::shared_ptr<Messenger>& messenger() const {
    return messenger_;
  }

  int attempt_num() const { return attempt_num_; }

  // Called when an RPC comes up for retrying. Actually sends the RPC.
  void DelayedRetryCb(Rpc* rpc, const Status& status);

 private:
  // The next sent rpc will be the nth attempt (indexed from 1).
  int attempt_num_;

  // If the remote end is busy, the RPC will be retried (with a small
  // delay) until this deadline is reached.
  //
  // May be uninitialized.
  MonoTime deadline_;

  // Messenger to use when sending the RPC.
  std::shared_ptr<Messenger> messenger_;

  // RPC controller to use when sending the RPC.
  RpcController controller_;

  // In case any retries have already happened, remembers the last error.
  // Errors from the server take precedence over timeout errors.
  Status last_error_;

  DISALLOW_COPY_AND_ASSIGN(RpcRetrier);
};

// An in-flight remote procedure call to some server.
class Rpc {
 public:
  Rpc(const MonoTime& deadline,
      const std::shared_ptr<rpc::Messenger>& messenger)
  : retrier_(deadline, messenger) {
  }

  virtual ~Rpc() {}

  // Asynchronously sends the RPC to the remote end.
  //
  // Subclasses should use SendRpcCb() below as the callback function.
  virtual void SendRpc() = 0;

  // Returns a string representation of the RPC.
  virtual std::string ToString() const = 0;

  // Returns the number of times this RPC has been sent. Will always be at
  // least one.
  int num_attempts() const { return retrier().attempt_num(); }

 protected:
  const RpcRetrier& retrier() const { return retrier_; }
  RpcRetrier* mutable_retrier() { return &retrier_; }

 private:
  friend class RpcRetrier;

  // Callback for SendRpc(). If 'status' is not OK, something failed
  // before the RPC was sent.
  virtual void SendRpcCb(const Status& status) = 0;

  // Used to retry some failed RPCs.
  RpcRetrier retrier_;

  DISALLOW_COPY_AND_ASSIGN(Rpc);
};

} // namespace rpc
} // namespace kudu

#endif // KUDU_RPC_RPC_H
