//
// Copyright (c) YugaByte, Inc.
//

#ifndef YB_RPC_RPC_FWD_H
#define YB_RPC_RPC_FWD_H

#include <boost/intrusive_ptr.hpp>

#include "yb/gutil/ref_counted.h"

namespace yb {
namespace rpc {

class AcceptorPool;
class Messenger;
class RpcContext;
class RpcController;

class Connection;
typedef boost::intrusive_ptr<Connection> ConnectionPtr;

class InboundCall;
typedef scoped_refptr<InboundCall> InboundCallPtr;

class OutboundCall;
typedef scoped_refptr<OutboundCall> OutboundCallPtr;

struct RedisClientCommand;

class ErrorStatusPB;

} // namespace rpc
} // namespace yb

#endif // YB_RPC_RPC_FWD_H
