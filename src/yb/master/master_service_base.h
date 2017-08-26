// Copyright (c) YugaByte, Inc.

#ifndef YB_MASTER_MASTER_SERVICE_BASE_H
#define YB_MASTER_MASTER_SERVICE_BASE_H

#include "yb/gutil/macros.h"

namespace yb {
namespace master {

class Master;
class CatalogManager;

// Base class for any master service with a few helpers.
class MasterServiceBase {
 public:
  explicit MasterServiceBase(Master* server) : server_(server) {}

 protected:
  // If 's' is not OK and 'resp' has no application specific error set,
  // set the error field of 'resp' to match 's' and set the code to
  // UNKNOWN_ERROR.
  template<class RespClass>
  static void CheckRespErrorOrSetUnknown(const Status& s, RespClass* resp);

  template <class ReqType, class RespType, class FnType>
  void HandleOnLeader(const ReqType* req, RespType* resp, rpc::RpcContext* rpc, FnType f);

  template <class HandlerType, class ReqType, class RespType>
  void HandleIn(const ReqType* req, RespType* resp, rpc::RpcContext* rpc,
      Status (HandlerType::*f)(RespType*));

  template <class HandlerType, class ReqType, class RespType>
  void HandleIn(const ReqType* req, RespType* resp, rpc::RpcContext* rpc,
      Status (HandlerType::*f)(const ReqType*, RespType*));

  template <class HandlerType, class ReqType, class RespType>
  void HandleIn(const ReqType* req, RespType* resp, rpc::RpcContext* rpc,
      Status (HandlerType::*f)(const ReqType*, RespType*, rpc::RpcContext*));

  YB_EDITION_NS_PREFIX CatalogManager* handler(CatalogManager*);

  Master* server_;

 private:
  DISALLOW_COPY_AND_ASSIGN(MasterServiceBase);
};

} // namespace master
} // namespace yb

#endif // YB_MASTER_MASTER_SERVICE_BASE_H
