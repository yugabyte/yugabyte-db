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

#include "yb/client/ql-dml-test-base.h"
#include "yb/client/table_handle.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/secure_stream.h"

#include "yb/server/secure.h"

#include "yb/util/env_util.h"

#include "yb/yql/cql/ql/util/errcodes.h"
#include "yb/yql/cql/ql/util/statement_result.h"

DECLARE_bool(use_client_to_server_encryption);
DECLARE_bool(use_node_to_node_encryption);
DECLARE_bool(allow_insecure_connections);
DECLARE_string(certs_dir);

namespace yb {

class SecureConnectionTest : public client::KeyValueTableTest {
 public:
  SecureConnectionTest() {
  }

  void SetUp() override {
    FLAGS_use_node_to_node_encryption = true;
    FLAGS_use_client_to_server_encryption = true;
    FLAGS_allow_insecure_connections = false;
    std::string test_executable_path;
    const auto sub_dir = JoinPathSegments("ent", "test_certs");
    auto root_dir = env_util::GetRootDir(sub_dir);
    FLAGS_certs_dir = JoinPathSegments(root_dir, sub_dir);

    client::QLDmlTestBase::SetUp();

    CreateTable(client::Transactional::kFalse);
  }

  CHECKED_STATUS CreateClient() override {
    rpc::MessengerBuilder messenger_builder("test_client");
    secure_context_ = VERIFY_RESULT(server::SetupSecureContext(
        "", "", server::SecureContextType::kClientToServer, &messenger_builder));
    auto messenger = VERIFY_RESULT(messenger_builder.Build());
    client::YBClientBuilder builder;
    builder.use_messenger(messenger);
    return cluster_->CreateClient(&builder, &client_);
  }

  std::unique_ptr<rpc::SecureContext> secure_context_;
};

TEST_F(SecureConnectionTest, Simple) {
  DontVerifyClusterBeforeNextTearDown(); // Verify requires insecure connection.

  const int32_t kKey = 1;
  const int32_t kValue = 2;

  {
    auto session = NewSession();
    auto op = ASSERT_RESULT(WriteRow(session, kKey, kValue));
    ASSERT_EQ(op->response().status(), QLResponsePB::YQL_STATUS_OK);
  }

  {
    auto value = ASSERT_RESULT(SelectRow(NewSession(), kKey));
    ASSERT_EQ(kValue, value);
  }
}

} // namespace yb
