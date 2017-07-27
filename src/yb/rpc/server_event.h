// Copyright (c) YugaByte, Inc.

#ifndef YB_RPC_SERVER_EVENT_H
#define YB_RPC_SERVER_EVENT_H

#include <vector>

#include "yb/rpc/rpc_call.h"
#include "yb/util/slice.h"

namespace yb {
namespace rpc {

class ServerEvent {
 public:
  virtual ~ServerEvent() {}
  // Serializes the data to be sent out via the RPC framework.
  virtual void Serialize(std::deque<RefCntBuffer> *output) const = 0;
  virtual std::string ToString() const = 0;
};

class ServerEventList : public OutboundData {
 public:
  virtual ~ServerEventList() {}
  virtual void Serialize(std::deque<RefCntBuffer> *output) const = 0;
  virtual std::string ToString() const = 0;
};

typedef std::shared_ptr<ServerEventList> ServerEventListPtr;

}  // namespace rpc
}  // namespace yb
#endif // YB_RPC_SERVER_EVENT_H
