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

#pragma once

#include <functional>

#include "yb/gutil/macros.h"

#include "yb/master/master_fwd.h"
#include "yb/rpc/rpc_fwd.h"

#include "yb/util/strongly_typed_bool.h"

namespace yb {

class Status;

namespace master {

class Master;
class CatalogManager;
class FlushManager;
class YsqlBackendsManager;
class PermissionsManager;
class EncryptionManager;
struct LeaderEpoch;
class XClusterManager;

// Tells HandleIn/HandleOnLeader to either acquire the lock briefly to check leadership (kFalse)
// or to hold it throughout the handler invocation (kTrue).
YB_STRONGLY_TYPED_BOOL(HoldCatalogLock);

// Base class for any master service with a few helpers.
class MasterServiceBase {
 public:
  explicit MasterServiceBase(Master* server) : server_(server) {}

 protected:
  template <class RespType, class FnType>
  void HandleOnLeader(
      RespType* resp,
      rpc::RpcContext* rpc,
      FnType f,
      const char* file_name,
      int line_number,
      const char* function_name,
      HoldCatalogLock hold_catalog_lock);

  template <class HandlerType, class ReqType, class RespType>
  void HandleOnAllMasters(
      const ReqType* req,
      RespType* resp,
      rpc::RpcContext* rpc,
      Status (HandlerType::*f)(const ReqType*, RespType*),
      const char* file_name,
      int line_number,
      const char* function_name);

  template <class HandlerType, class ReqType, class RespType>
  void HandleIn(
      const ReqType* req,
      RespType* resp,
      rpc::RpcContext* rpc,
      Status (HandlerType::*f)(RespType*),
      const char* file_name,
      int line,
      const char* function_name,
      HoldCatalogLock hold_catalog_lock);

  template <class HandlerType, class ReqType, class RespType>
  void HandleIn(
      const ReqType* req,
      RespType* resp,
      rpc::RpcContext* rpc,
      Status (HandlerType::*f)(const ReqType*, RespType*),
      const char* file_name,
      int line_number,
      const char* function_name,
      HoldCatalogLock hold_catalog_lock);

  template <class HandlerType, class ReqType, class RespType>
  void HandleIn(
      const ReqType* req,
      RespType* resp,
      rpc::RpcContext* rpc,
      Status (HandlerType::*f)(const ReqType*, RespType*, rpc::RpcContext*),
      const char* file_name,
      int line_number,
      const char* function_name,
      HoldCatalogLock hold_catalog_lock);

  template <class HandlerType, class ReqType, class RespType>
  void HandleIn(
      const ReqType* req,
      RespType* resp,
      rpc::RpcContext* rpc,
      Status (HandlerType::*f)(
          const ReqType*, RespType*, rpc::RpcContext*, const LeaderEpoch&),
      const char* file_name,
      int line_number,
      const char* function_name,
      HoldCatalogLock hold_catalog_lock);

  CatalogManager* handler(CatalogManager*);
  FlushManager* handler(FlushManager*);
  YsqlBackendsManager* handler(YsqlBackendsManager*);
  PermissionsManager* handler(PermissionsManager*);
  EncryptionManager* handler(EncryptionManager*);
  XClusterManager* handler(XClusterManager*);
  TestAsyncRpcManager* handler(TestAsyncRpcManager*);

  Master* server_;

 private:
  DISALLOW_COPY_AND_ASSIGN(MasterServiceBase);
};

} // namespace master
} // namespace yb
