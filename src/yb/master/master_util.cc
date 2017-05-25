// Copyright (c) YugaByte, Inc.

#include "yb/master/master_util.h"

#include "yb/common/wire_protocol.h"
#include "yb/master/master.proxy.h"
#include "yb/master/master.service.h"
#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"

namespace yb {

using master::GetMasterRegistrationRequestPB;
using master::GetMasterRegistrationResponsePB;
using master::MasterServiceProxy;

Status MasterUtil::GetMasterEntryForHost(const std::shared_ptr<rpc::Messenger>& messenger,
                                         const HostPort& hostport,
                                         int timeout,
                                         ServerEntryPB* e) {
  Endpoint sockaddr;
  RETURN_NOT_OK(EndpointFromHostPort(hostport, &sockaddr));
  MasterServiceProxy proxy(messenger, sockaddr);
  GetMasterRegistrationRequestPB req;
  GetMasterRegistrationResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(MonoDelta::FromMilliseconds(timeout));
  RETURN_NOT_OK(proxy.GetMasterRegistration(req, &resp, &controller));
  e->mutable_instance_id()->CopyFrom(resp.instance_id());
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  e->mutable_registration()->CopyFrom(resp.registration());
  e->set_role(resp.role());
  return Status::OK();
}

} // namespace yb
