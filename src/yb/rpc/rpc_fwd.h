//
// Copyright (c) YugaByte, Inc.
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
//

#pragma once

#include <chrono>
#include <functional>
#include <unordered_map>

#include <boost/functional/hash.hpp>
#include <boost/container/small_vector.hpp>

#include "yb/gutil/ref_counted.h"

#include "yb/util/enums.h"
#include "yb/util/math_util.h"
#include "yb/util/slice.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {

class RefCntBuffer;
class RefCntSlice;
class Slice;

namespace rpc {

class Acceptor;
class AcceptorPool;
class AnyMessageConstPtr;
class AnyMessagePtr;
class CallResponse;
class ConnectionContext;
class DelayedTask;
class DumpRunningRpcsRequestPB;
class DumpRunningRpcsResponsePB;
class GrowableBufferAllocator;
class LightweightMessage;
class MessengerBuilder;
class PeriodicTimer;
class Proxy;
class ProxyCache;
class ProxyContext;
class Reactor;
class ReactorTask;
class ReceivedSidecars;
class RpcCallParams;
class RemoteMethod;
class RequestHeader;
class ResponseHeader;
class RpcConnectionPB;
class RpcContext;
class RpcController;
class Rpcs;
class Poller;
class Protocol;
class Proxy;
class ProxySource;
class RefinedStream;
class Scheduler;
class SecureContext;
class ServicePoolImpl;
class Sidecars;
class Strand;
class StrandTask;
class Stream;
class StreamReadBuffer;
class ThreadPool;
class ThreadPoolTask;
class LocalYBInboundCall;

struct CallData;
struct OutboundCallMetrics;
struct OutboundMethodMetrics;
struct ProcessCallsResult;
struct ReactorMetrics;
struct RpcMethodMetrics;
struct RpcMetrics;

class RpcService;
using RpcServicePtr = scoped_refptr<RpcService>;
using RpcEndpointMap = std::unordered_map<
    Slice, std::pair<RpcServicePtr, size_t>, boost::hash<Slice>>;

class RpcCommand;
typedef std::shared_ptr<RpcCommand> RpcCommandPtr;

class Connection;
class ConnectionContext;
typedef std::shared_ptr<Connection> ConnectionPtr;
typedef std::weak_ptr<Connection> ConnectionWeakPtr;

class InboundCall;
typedef std::shared_ptr<InboundCall> InboundCallPtr;

class Messenger;

class OutboundCall;
typedef std::shared_ptr<OutboundCall> OutboundCallPtr;
typedef std::weak_ptr<OutboundCall> OutboundCallWeakPtr;

class OutboundData;
typedef std::shared_ptr<OutboundData> OutboundDataPtr;

class ServerEventList;
typedef std::shared_ptr<ServerEventList> ServerEventListPtr;

class ServiceIf;
typedef std::shared_ptr<ServiceIf> ServiceIfPtr;

typedef std::function<int(const std::string&, const std::string&)> Publisher;

using ConnectionFilter = std::function<bool(const ConnectionPtr&)>;

// SteadyTimePoint is something like MonoTime, but 3rd party libraries know it and don't know about
// our private MonoTime.
typedef std::chrono::steady_clock::time_point SteadyTimePoint;

class ConnectionContextFactory;
typedef std::shared_ptr<ConnectionContextFactory> ConnectionContextFactoryPtr;

class StreamFactory;
typedef std::shared_ptr<StreamFactory> StreamFactoryPtr;

YB_STRONGLY_TYPED_BOOL(ReadBufferFull);
YB_STRONGLY_TYPED_BOOL(Queue);

typedef int64_t ScheduledTaskId;
constexpr ScheduledTaskId kInvalidTaskId = -1;

using ProxyPtr = std::shared_ptr<Proxy>;
using ResponseCallback = std::function<void()>;

YB_DEFINE_ENUM(ServicePriority, (kNormal)(kHigh));

// Specifies how to run callback for async outbound call.
YB_DEFINE_ENUM(InvokeCallbackMode,
    // On reactor thread.
    (kReactorThread)
    // On thread pool.
    (kThreadPoolNormal)
    (kThreadPoolHigh));

using SidecarHolder = std::pair<RefCntBuffer, Slice>;
using CallResponsePtr = std::shared_ptr<CallResponse>;
using RpcCallParamsPtr = std::shared_ptr<RpcCallParams>;
using ByteBlocks = boost::container::small_vector_base<RefCntSlice>;

} // namespace rpc
} // namespace yb
