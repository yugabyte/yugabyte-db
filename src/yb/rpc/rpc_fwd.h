//
// Copyright (c) YugaByte, Inc.
//

#ifndef YB_RPC_RPC_FWD_H
#define YB_RPC_RPC_FWD_H

#include <chrono>

#include <boost/intrusive_ptr.hpp>

#include "yb/gutil/ref_counted.h"

namespace boost {
namespace asio {

class io_service;

} // namespace asio
} // namespace boost

namespace yb {
namespace rpc {

class AcceptorPool;
class Messenger;
class RpcCommand;
class RpcContext;
class RpcController;
class Scheduler;

class Connection;
typedef std::shared_ptr<Connection> ConnectionPtr;

class InboundCall;
typedef std::shared_ptr<InboundCall> InboundCallPtr;

class OutboundCall;
typedef std::shared_ptr<OutboundCall> OutboundCallPtr;

class ErrorStatusPB;

typedef boost::asio::io_service IoService;

// SteadyTimePoint is something like MonoTime, but 3rd party libraries know it and don't know about
// our private MonoTime.
typedef std::chrono::steady_clock::time_point SteadyTimePoint;

} // namespace rpc
} // namespace yb

#endif // YB_RPC_RPC_FWD_H
