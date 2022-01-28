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

#ifndef ENT_SRC_YB_INTEGRATION_TESTS_CDCSDK_TEST_BASE_H
#define ENT_SRC_YB_INTEGRATION_TESTS_CDCSDK_TEST_BASE_H

#include <string>

#include "yb/client/transaction_manager.h"

#include "yb/integration-tests/cdc_test_util.h"
#include "yb/integration-tests/mini_cluster.h"

#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"

DECLARE_int32(cdc_read_rpc_timeout_ms);
DECLARE_int32(cdc_write_rpc_timeout_ms);
DECLARE_bool(TEST_check_broadcast_address);
DECLARE_bool(flush_rocksdb_on_shutdown);
DECLARE_bool(cdc_enable_replicate_intents);

namespace yb {
using client::YBClient;

namespace cdc {
namespace enterprise {
constexpr int kRpcTimeout = NonTsanVsTsan(60, 120);
static const std::string kUniverseId = "test_universe";
static const std::string kNamespaceName = "test_namespace";

struct CDCSDKTestParams {
  CDCSDKTestParams(int batch_size_, bool enable_replicate_intents_) :
      batch_size(batch_size_), enable_replicate_intents(enable_replicate_intents_) {}

  int batch_size;
  bool enable_replicate_intents;
};

class CDCSDKTestBase : public YBTest {
 public:
  class Cluster {
   public:
    std::unique_ptr<MiniCluster> mini_cluster_;
    std::unique_ptr<YBClient> client_;
    std::unique_ptr<yb::pgwrapper::PgSupervisor> pg_supervisor_;
    HostPort pg_host_port_;
    boost::optional<client::TransactionManager> txn_mgr_;

    Result<pgwrapper::PGConn> Connect() {
      return pgwrapper::PGConn::Connect(pg_host_port_);
    }

    Result<pgwrapper::PGConn> ConnectToDB(const std::string& dbname) {
      return pgwrapper::PGConn::Connect(pg_host_port_, dbname);
    }
  };

  void SetUp() override {
    YBTest::SetUp();
    // Allow for one-off network instability by ensuring a single CDC RPC timeout << test timeout.
    FLAGS_cdc_read_rpc_timeout_ms = (kRpcTimeout / 4) * 1000;
    FLAGS_cdc_write_rpc_timeout_ms = (kRpcTimeout / 4) * 1000;
    // Not a useful test for us. It's testing Public+Private IP NW errors and we're only public
    FLAGS_TEST_check_broadcast_address = false;
    FLAGS_flush_rocksdb_on_shutdown = false;
  }

  void TearDown() override;

  std::unique_ptr<CDCServiceProxy> GetCdcProxy();

  MiniCluster* test_cluster() {
    return test_cluster_.mini_cluster_.get();
  }

  client::TransactionManager* test_cluster_txn_mgr() {
    return test_cluster_.txn_mgr_.get_ptr();
  }

  YBClient* test_client() {
    return test_cluster_.client_.get();
  }

 protected:
  Cluster test_cluster_;
};
} // namespace enterprise
} // namespace cdc
} // namespace yb

#endif  // ENT_SRC_YB_INTEGRATION_TESTS_CDCSDK_TEST_BASE_H
