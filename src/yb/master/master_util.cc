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

Status GetMasterEntryForHost(rpc::ProxyCache* proxy_cache,
                             const HostPort& hostport,
                             int timeout,
                             ServerEntryPB* e) {
  MasterServiceProxy proxy(proxy_cache, hostport);
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
