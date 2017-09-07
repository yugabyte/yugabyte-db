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

#include "yb/rpc/negotiation.h"

#include <sys/time.h>
#include <poll.h>

#include <string>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rpc/blocking_ops.h"
#include "yb/rpc/connection.h"
#include "yb/rpc/reactor.h"
#include "yb/rpc/rpc_header.pb.h"
#include "yb/rpc/sasl_client.h"
#include "yb/rpc/sasl_common.h"
#include "yb/rpc/sasl_server.h"
#include "yb/rpc/yb_rpc.h"
#include "yb/util/flag_tags.h"
#include "yb/util/status.h"
#include "yb/util/trace.h"

DEFINE_bool(rpc_trace_negotiation, false,
            "If enabled, dump traces of all RPC negotiations to the log");
TAG_FLAG(rpc_trace_negotiation, runtime);
TAG_FLAG(rpc_trace_negotiation, advanced);
TAG_FLAG(rpc_trace_negotiation, experimental);

namespace yb {
namespace rpc {

namespace {

using std::shared_ptr;
using strings::Substitute;

// Client: Send ConnectionContextPB message based on information stored in the Connection object.
Status SendConnectionContext(Connection *conn, const MonoTime &deadline) {
  TRACE("Sending connection context");
  RequestHeader header;
  header.set_call_id(kConnectionContextCallId);

  ConnectionContextPB conn_context;
  conn_context.mutable_user_info()->set_effective_user(conn->user_credentials().effective_user());
  conn_context.mutable_user_info()->set_real_user(conn->user_credentials().real_user());

  return SendFramedMessageBlocking(conn->socket(), header, conn_context, deadline);
}

// Server: Receive ConnectionContextPB message and update the corresponding fields in the
// associated Connection object. Perform validation against SASL-negotiated information
// as needed.
Status RecvConnectionContext(Connection *conn,
                             YBConnectionContext *context,
                             const MonoTime &deadline) {
  TRACE("Waiting for connection context");
  faststring recv_buf(1024); // Should be plenty for a ConnectionContextPB message.
  RequestHeader header;
  Slice param_buf;
  RETURN_NOT_OK(ReceiveFramedMessageBlocking(conn->socket(), &recv_buf,
      &header, &param_buf, deadline));
  DCHECK(header.IsInitialized());

  if (header.call_id() != kConnectionContextCallId) {
    return STATUS(IllegalState, "Expected ConnectionContext callid, received",
        Substitute("$0", header.call_id()));
  }

  ConnectionContextPB conn_context;
  if (!conn_context.ParseFromArray(param_buf.data(), param_buf.size())) {
    return STATUS(InvalidArgument, "Invalid ConnectionContextPB message, missing fields",
        conn_context.InitializationErrorString());
  }

  // Update the fields of our Connection object from the ConnectionContextPB.
  if (conn_context.has_user_info()) {
    // Validate real user against SASL impl.
    if (context->sasl_server().negotiated_mechanism() == SaslMechanism::PLAIN) {
      if (context->sasl_server().plain_auth_user() != conn_context.user_info().real_user()) {
        return STATUS(NotAuthorized,
            "ConnectionContextPB specified different real user than sent in SASL negotiation",
            StringPrintf("\"%s\" vs. \"%s\"",
                conn_context.user_info().real_user().c_str(),
                context->sasl_server().plain_auth_user().c_str()));
      }
    }
    conn->mutable_user_credentials()->set_real_user(conn_context.user_info().real_user());

    // TODO: Validate effective user when we implement impersonation.
    if (conn_context.user_info().has_effective_user()) {
      conn->mutable_user_credentials()->set_effective_user(
          conn_context.user_info().effective_user());
    }
  }
  return Status::OK();
}

// Wait for the client connection to be established and become ready for writing.
Status WaitForClientConnect(Connection *conn, const MonoTime &deadline) {
  TRACE("Waiting for socket to connect");
  int fd = conn->socket()->GetFd();
  struct pollfd poll_fd;
  poll_fd.fd = fd;
  poll_fd.events = POLLOUT;
  poll_fd.revents = 0;

  MonoTime now;
  MonoDelta remaining;
  bool have_timeout = !deadline.IsMax();
  while (true) {
    if (have_timeout) {
      now = MonoTime::Now(MonoTime::FINE);
      remaining = deadline.GetDeltaSince(now);
      DVLOG(4) << "Client waiting to connect for negotiation, "
               << "time remaining until timeout deadline: " << remaining.ToString();
      if (PREDICT_FALSE(remaining.ToNanoseconds() <= 0)) {
        return STATUS(TimedOut, "Timeout exceeded waiting to connect");
      }
    }
#if defined(__linux__)
    struct timespec ts;
    struct timespec* ts_ptr = nullptr;
    if (have_timeout) {
      remaining.ToTimeSpec(&ts);
      ts_ptr = &ts;
    }
    int ready = ppoll(&poll_fd, 1, ts_ptr, nullptr);
#else
    int32_t timeout = -1;
    if (have_timeout) {
      int64_t remaining_ms = remaining.ToMilliseconds();
      CHECK(remaining_ms >= 0 && remaining_ms <= std::numeric_limits<int32>::max())
          << "Invalid timeout: " << remaining_ms;
      timeout = static_cast<int32_t>(remaining_ms);
    }
    int ready = poll(&poll_fd, 1, timeout);
#endif
    if (ready == -1) {
      int err = errno;
      if (err == EINTR) {
        // We were interrupted by a signal, let's go again.
        continue;
      } else {
        return STATUS(NetworkError, "Error from ppoll() while waiting to connect",
            ErrnoToString(err), err);
      }
    } else if (ready == 0) {
      // Timeout exceeded. Loop back to the top to our impending doom.
      continue;
    } else {
      // Success.
      break;
    }
  }

  // Connect finished, but this doesn't mean that we connected successfully.
  // Check the socket for an error.
  int so_error = 0;
  socklen_t socklen = sizeof(so_error);
  int rc = getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &socklen);
  if (rc != 0) {
    return STATUS(NetworkError, "Unable to check connected socket for errors",
        ErrnoToString(errno),
        errno);
  }
  if (so_error != 0) {
    return STATUS(NetworkError, "connect", ErrnoToString(so_error), so_error);
  }

  return Status::OK();
}

// Perform client negotiation. We don't LOG() anything, we leave that to our caller.
Status DoClientNegotiation(Connection *conn,
                           YBConnectionContext *context,
                           const MonoTime &deadline) {
  RETURN_NOT_OK(WaitForClientConnect(conn, deadline));
  RETURN_NOT_OK(conn->SetNonBlocking(false));
  RETURN_NOT_OK(context->InitSaslClient(conn));
  context->sasl_client().set_deadline(deadline);
  RETURN_NOT_OK(context->sasl_client().Negotiate());
  RETURN_NOT_OK(SendConnectionContext(conn, deadline));

  return Status::OK();
}

// Perform server negotiation. We don't LOG() anything, we leave that to our caller.
static Status DoServerNegotiation(Connection *conn,
                                  YBConnectionContext *context,
                                  const MonoTime &deadline) {
  RETURN_NOT_OK(conn->SetNonBlocking(false));
  RETURN_NOT_OK(context->InitSaslServer(conn));
  context->sasl_server().set_deadline(deadline);
  RETURN_NOT_OK(context->sasl_server().Negotiate());
  RETURN_NOT_OK(RecvConnectionContext(conn, context, deadline));

  return Status::OK();
}

} // namespace

void Negotiation::RunNegotiation(ConnectionPtr conn,
                                 const MonoTime& deadline) {
  conn->RunNegotiation(deadline);
}

void Negotiation::YBNegotiation(ConnectionPtr conn,
                                YBConnectionContext* context,
                                const MonoTime& deadline) {
  Status s;
  if (conn->direction() == ConnectionDirection::SERVER) {
    s = DoServerNegotiation(conn.get(), context, deadline);
  } else {
    s = DoClientNegotiation(conn.get(), context, deadline);
  }

  if (PREDICT_FALSE(!s.ok())) {
    string msg = Substitute("$0 connection negotiation failed: $1",
                            conn->direction() == ConnectionDirection::SERVER ? "Server" : "Client",
                            conn->ToString());
    s = s.CloneAndPrepend(msg);
  }
  TRACE("Negotiation complete: $0", s.ToString());

  bool is_bad = !s.ok() && !(s.IsNetworkError() && s.posix_code() == ECONNREFUSED);

  if (is_bad || FLAGS_rpc_trace_negotiation) {
    string msg =
      Trace::CurrentTrace() ? Trace::CurrentTrace()->DumpToString(true) : "No trace found!";
    if (is_bad) {
      LOG(WARNING) << "Failed RPC negotiation. Trace:\n" << msg;
    } else {
      LOG(INFO) << "RPC negotiation tracing enabled. Trace:\n" << msg;
    }
  }
  conn->CompleteNegotiation(std::move(s));
}


} // namespace rpc
} // namespace yb
