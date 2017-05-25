// Copyright (c) YugaByte, Inc.

#ifndef YB_CONSENSUS_CONSENSUS_UTIL_H
#define YB_CONSENSUS_CONSENSUS_UTIL_H

namespace yb {
namespace consensus {

// Specifies whether to send empty consensus requests from the leader to followers in case the queue
// is empty.
enum class RequestTriggerMode {
  // Only send a request if it is not empty.
  NON_EMPTY_ONLY,

  // Send a request even if the queue is empty, and therefore (in most cases) the request is empty.
  // This is used during heartbeats from leader to peers.
  ALWAYS_SEND
};

}  // namespace consensus
}  // namespace yb

#endif // YB_CONSENSUS_CONSENSUS_UTIL_H
