// Copyright (c) YugaByte, Inc.

#ifndef YB_RPC_OUTBOUND_DATA_H
#define YB_RPC_OUTBOUND_DATA_H

#include "yb/gutil/ref_counted.h"
#include "yb/util/slice.h"

namespace yb {
namespace rpc {

class OutboundData;

// Interface for outbound transfers from the RPC framework.
class OutboundData : public RefCountedThreadSafe<OutboundData> {
 public:
  virtual ~OutboundData() {}
  // Serializes the data to be sent out via the RPC framework.
  virtual void Serialize(std::vector<Slice>* slices) const = 0;
  virtual std::string ToString() const = 0;
};

typedef scoped_refptr<OutboundData> OutboundDataPtr;

}  // namespace rpc
}  // namespace yb

#endif // YB_RPC_OUTBOUND_DATA_H
