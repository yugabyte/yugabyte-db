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

#include "yb/tserver/stateful_services/test_echo_service.h"
#include "yb/client/client.h"
#include "yb/client/session.h"
#include "yb/client/yb_op.h"
#include "yb/gutil/walltime.h"
#include "yb/master/master_defaults.h"

using namespace std::chrono_literals;

namespace yb {

namespace stateful_service {
TestEchoService::TestEchoService(
    const std::string& node_uuid, const scoped_refptr<MetricEntity>& metric_entity,
    const std::shared_future<client::YBClient*>& client_future)
    : StatefulRpcServiceBase(StatefulServiceKind::TEST_ECHO, metric_entity, client_future),
      node_uuid_(node_uuid) {}

void TestEchoService::Activate(const int64_t leader_term) {
  LOG(INFO) << "Test Echo service activated on term: " << leader_term;
}

void TestEchoService::Deactivate() { LOG(INFO) << "Test Echo service de-activated"; }

Result<bool> TestEchoService::RunPeriodicTask() {
  LOG(INFO) << "Test Echo service Running";
  return true;
}

Status TestEchoService::GetEchoImpl(const GetEchoRequestPB& req, GetEchoResponsePB* resp) {
  std::string echo = req.message();

  RETURN_NOT_OK(RecordRequestInTable(echo));

  // For a string to bounce back and make an echo, there has to be a lot of latency between the
  // string source and the thing (wall or mountain or service) that it hits and bounces back. Since
  // latency in Yugabyte is very low we need to do some string manipulation instead.
  auto loc = echo.find_last_of(' ') + 1;
  auto last_word = " " + echo.substr(loc, echo.size() - loc);
  echo.append(last_word).append(last_word);

  resp->set_message(std::move(echo));
  resp->set_node_id(node_uuid_);

  return Status::OK();
}

Status TestEchoService::RecordRequestInTable(const std::string& message) {
  auto* table = VERIFY_RESULT(GetServiceTable());

  const auto op = table->NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
  auto* const req = op->mutable_request();
  QLAddTimestampHashValue(req, GetCurrentTimeMicros());
  table->AddStringColumnValue(req, master::kTestEchoNodeId, node_uuid_);
  table->AddStringColumnValue(req, master::kTestEchoMessage, message);

  std::shared_ptr<client::YBSession> session = GetYBSession();
  session->SetTimeout(30s);
  return session->TEST_ApplyAndFlush(op);
}

}  // namespace stateful_service
}  // namespace yb
