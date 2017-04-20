//
// Copyright (c) YugaByte, Inc.
//

#ifndef YB_RPC_RPC_CALL_H
#define YB_RPC_RPC_CALL_H

#include "yb/gutil/ref_counted.h"

namespace yb {
namespace rpc {

class RpcCall : public RefCountedThreadSafe<RpcCall> {
 public:
  virtual ~RpcCall() = default;
};

typedef scoped_refptr<RpcCall> RpcCallPtr;

} // namespace rpc
} // namespace yb

#endif // YB_RPC_RPC_CALL_H
