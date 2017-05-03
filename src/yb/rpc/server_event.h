// Copyright (c) YugaByte, Inc.

#ifndef YB_RPC_SERVER_EVENT_H
#define YB_RPC_SERVER_EVENT_H

#include <vector>

#include "yb/rpc/outbound_data.h"
#include "yb/util/slice.h"

namespace yb {
namespace rpc {

class ServerEvent : public OutboundData {
 public:
  virtual ~ServerEvent() {}
  virtual void Serialize(std::deque<util::RefCntBuffer>* buffers) const = 0;
  virtual std::string ToString() const = 0;
};

}  // namespace rpc
}  // namespace yb
#endif // YB_RPC_SERVER_EVENT_H
