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

#ifndef YB_CLIENT_QL_DML_TEST_BASE_H
#define YB_CLIENT_QL_DML_TEST_BASE_H

#include <algorithm>
#include <functional>
#include <vector>

#include <gtest/gtest.h>

#include "yb/client/yb_op.h"
#include "yb/client/callbacks.h"
#include "yb/common/ql_protocol.pb.h"
#include "yb/common/ql_rowblock.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/master/mini_master.h"
#include "yb/util/async_util.h"
#include "yb/util/test_util.h"

namespace yb {
namespace client {

using std::string;
using std::vector;
using std::shared_ptr;
using std::unique_ptr;

extern const client::YBTableName kTableName;

Status FlushSession(YBSession *session);

class QLDmlTestBase: public YBMiniClusterTestBase<MiniCluster> {
 public:
  void SetUp() override;
  void DoTearDown() override;

 protected:
  shared_ptr<YBClient> client_;
};

}  // namespace client
}  // namespace yb

#endif // YB_CLIENT_QL_DML_TEST_BASE_H
