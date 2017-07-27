// Copyright (c) YugaByte, Inc.

#ifndef YB_RPC_OUTBOUND_DATA_H
#define YB_RPC_OUTBOUND_DATA_H

#include <deque>
#include <memory>

#include "yb/util/ref_cnt_buffer.h"

namespace yb {

class Status;

namespace rpc {

// Interface for outbound transfers from the RPC framework.
class OutboundData : public std::enable_shared_from_this<OutboundData> {
 public:
  virtual void Transferred(const Status& status) = 0;

  virtual ~OutboundData() {}
  // Serializes the data to be sent out via the RPC framework.
  virtual void Serialize(std::deque<RefCntBuffer> *output) const = 0;
  virtual std::string ToString() const = 0;
};

typedef std::shared_ptr<OutboundData> OutboundDataPtr;

}  // namespace rpc
}  // namespace yb

#endif // YB_RPC_OUTBOUND_DATA_H
