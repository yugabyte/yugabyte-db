// Copyright (c) YugabyteDB, Inc.
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

#include "yb/master/master_ysql_lease_client.h"

#include "yb/common/wire_protocol.h"

namespace yb::master {

MasterYsqlLeaseClient::MasterYsqlLeaseClient(MasterYsqlLeaseProxy&& proxy) noexcept
    : proxy_(std::move(proxy)) {}

Result<RefreshYsqlLeaseInfoPB> MasterYsqlLeaseClient::RefreshYsqlLease(
    const std::string& permanent_uuid, int64_t instance_seqno, uint64_t time_ms,
    std::optional<uint64_t> current_lease_epoch) {
  RefreshYsqlLeaseRequestPB req;
  req.mutable_instance()->set_permanent_uuid(permanent_uuid);
  req.mutable_instance()->set_instance_seqno(instance_seqno);
  req.set_local_request_send_time_ms(time_ms);
  if (current_lease_epoch) {
    req.set_current_lease_epoch(*current_lease_epoch);
  }
  RefreshYsqlLeaseResponsePB resp;
  rpc::RpcController rpc;
  RETURN_NOT_OK(proxy_.RefreshYsqlLease(req, &resp, &rpc));
  RETURN_NOT_OK(ResponseStatus(resp));
  return resp.info();
}

Status MasterYsqlLeaseClient::RelinquishYsqlLease(
    const std::string& permanent_uuid, int64_t instance_seqno) {
  RelinquishYsqlLeaseRequestPB req;
  req.mutable_instance()->set_permanent_uuid(permanent_uuid);
  req.mutable_instance()->set_instance_seqno(instance_seqno);
  RelinquishYsqlLeaseResponsePB resp;
  rpc::RpcController rpc;
  RETURN_NOT_OK(proxy_.RelinquishYsqlLease(req, &resp, &rpc));
  return ResponseStatus(resp);
}

}  // namespace yb::master
