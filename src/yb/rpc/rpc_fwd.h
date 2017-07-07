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
typedef std::shared_ptr<Connection> ConnectionPtr;

class InboundCall;
typedef std::shared_ptr<InboundCall> InboundCallPtr;

class OutboundCall;
typedef std::shared_ptr<OutboundCall> OutboundCallPtr;

class ErrorStatusPB;

} // namespace rpc
} // namespace yb

#endif // YB_RPC_RPC_FWD_H
