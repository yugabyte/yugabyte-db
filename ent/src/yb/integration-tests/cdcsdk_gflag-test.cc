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

#include <algorithm>
#include <string>
#include <utility>
#include <chrono>
#include <boost/assign.hpp>
#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include "yb/common/common.pb.h"
#include "yb/common/entity_ids.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/cdc_service.pb.h"
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/client/client.h"
#include "yb/client/client-test-util.h"
#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/client/transaction.h"
#include "yb/client/yb_op.h"

#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/integration-tests/cdc_test_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/cdcsdk_test_base.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/cdc_consumer_registry_service.h"
#include "yb/master/sys_catalog_initialization.h"

#include "yb/rpc/rpc_controller.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/tserver/cdc_consumer.h"
#include "yb/util/atomic.h"
#include "yb/util/faststring.h"
#include "yb/util/format.h"
#include "yb/util/monotime.h"
#include "yb/util/random.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_util.h"
#include "yb/util/test_macros.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"

DECLARE_int32(replication_factor);
DECLARE_int32(cdc_max_apply_batch_num_records);
DECLARE_int32(client_read_write_timeout_ms);
DECLARE_int32(pgsql_proxy_webserver_port);
DECLARE_bool(enable_ysql);
DECLARE_bool(hide_pg_catalog_table_creation_logs);
DECLARE_bool(master_auto_run_initdb);
DECLARE_int32(pggate_rpc_timeout_secs);
DECLARE_int32(cdc_snapshot_batch_size);
DECLARE_int32(cdc_max_stream_intent_records);

namespace yb {
namespace cdc {
namespace enterprise {
class CDCSDKGFlagValueTest : public CDCSDKTestBase {
 public:
  Status SetUpWithParams(uint32_t replication_factor,
                         uint32_t num_masters = 1,
                         bool colocated = false) {
    master::SetDefaultInitialSysCatalogSnapshotFlags();
    CDCSDKTestBase::SetUp();
    FLAGS_enable_ysql = true;
    FLAGS_master_auto_run_initdb = true;
    FLAGS_hide_pg_catalog_table_creation_logs = true;
    FLAGS_pggate_rpc_timeout_secs = 120;
    FLAGS_cdc_max_apply_batch_num_records = 1;
    FLAGS_cdc_enable_replicate_intents = true;

    MiniClusterOptions opts;
    opts.num_tablet_servers = replication_factor;
    opts.num_masters = num_masters;
    FLAGS_replication_factor = replication_factor;
    opts.cluster_id = "cdcsdk_cluster";
    test_cluster_.mini_cluster_ = std::make_unique<MiniCluster>(opts);
        RETURN_NOT_OK(test_cluster()->StartSync());
        RETURN_NOT_OK(test_cluster()->WaitForTabletServerCount(replication_factor));
        RETURN_NOT_OK(WaitForInitDb(test_cluster()));
    test_cluster_.client_ = VERIFY_RESULT(test_cluster()->CreateClient());
        RETURN_NOT_OK(InitPostgres(&test_cluster_));
        RETURN_NOT_OK(CreateDatabase(&test_cluster_, kNamespaceName, colocated));

    LOG(INFO) << "Cluster created successfully for CDCSDK";
    return Status::OK();
  }

  Status InitPostgres(Cluster* cluster) {
    auto pg_ts = RandomElement(cluster->mini_cluster_->mini_tablet_servers());
    auto port = cluster->mini_cluster_->AllocateFreePort();
    yb::pgwrapper::PgProcessConf pg_process_conf =
        VERIFY_RESULT(yb::pgwrapper::PgProcessConf::CreateValidateAndRunInitDb(
            yb::ToString(Endpoint(pg_ts->bound_rpc_addr().address(), port)),
            pg_ts->options()->fs_opts.data_paths.front() + "/pg_data",
            pg_ts->server()->GetSharedMemoryFd()));
    pg_process_conf.master_addresses = pg_ts->options()->master_addresses_flag;
    pg_process_conf.force_disable_log_file = true;
    FLAGS_pgsql_proxy_webserver_port = cluster->mini_cluster_->AllocateFreePort();

    LOG(INFO) << "Starting PostgreSQL server listening on "
              << pg_process_conf.listen_addresses << ":" << pg_process_conf.pg_port << ", data: "
              << pg_process_conf.data_dir
              << ", pgsql webserver port: " << FLAGS_pgsql_proxy_webserver_port;
    cluster->pg_supervisor_ = std::make_unique<yb::pgwrapper::PgSupervisor>(pg_process_conf);
        RETURN_NOT_OK(cluster->pg_supervisor_->Start());

    cluster->pg_host_port_ = HostPort(pg_process_conf.listen_addresses, pg_process_conf.pg_port);
    return Status::OK();
  }

  Status CreateDatabase(Cluster* cluster,
                        const std::string& namespace_name = kNamespaceName,
                        bool colocated = false) {
    auto conn = EXPECT_RESULT(cluster->Connect());
    EXPECT_OK(conn.ExecuteFormat("CREATE DATABASE $0$1",
                                 namespace_name, colocated ? " colocated = true" : ""));
    return Status::OK();
  }
};

TEST_F(CDCSDKGFlagValueTest, TestDefaultIntentSizeGFlag) {
  // create a cluster
  ASSERT_OK(SetUpWithParams(3, 1, false));

  const uint32_t default_intent_batch_size = 1000;
  ASSERT_EQ(default_intent_batch_size, FLAGS_cdc_max_stream_intent_records);
}

TEST_F(CDCSDKGFlagValueTest, TestDefaultSnapshotBatchSizeGFlag) {
  // create a cluster
  ASSERT_OK(SetUpWithParams(3, 1, false));

  const uint32_t default_snapshot_batch_size = 250;
  ASSERT_EQ(default_snapshot_batch_size, FLAGS_cdc_snapshot_batch_size);
}
} // namespace enterprise
} // namespace cdc
} // namespace yb
