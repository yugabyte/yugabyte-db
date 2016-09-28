// Copyright (c) YugaByte, Inc.

#ifndef YB_INTEGRATION_TESTS_YB_TABLE_TEST_BASE
#define YB_INTEGRATION_TESTS_YB_TABLE_TEST_BASE

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <future>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include "yb/client/client.h"
#include "yb/client/callbacks.h"
#include "yb/client/client-test-util.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/strings/split.h"
#include "yb/gutil/strings/strcat.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/integration-tests/load_generator.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/master/mini_master.h"
#include "yb/tablet/maintenance_manager.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metrics.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/util/random.h"
#include "yb/util/random_util.h"
#include "yb/util/stopwatch.h"
#include "yb/util/subprocess.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace yb {
namespace integration_tests {

// This is a common base class from which SQLTableTest and RedisTableTest will inherit from.
// In future some of the functionality may be migrated to sub-base classes when it becomes bigger.
// i.e. scan related functions may be moved down because it is only supported for Kudu / SQL tables.
class YBTableTestBase : public YBTest {
 protected:
  YBTableTestBase();
  virtual void SetUp() OVERRIDE;
  virtual void TearDown() OVERRIDE;

  virtual bool use_external_mini_cluster();
  virtual int session_timeout_ms();
  virtual int num_masters();
  virtual int num_tablet_servers();
  virtual int client_rpc_timeout_ms();
  virtual string table_name();

  virtual void CreateTable();
  void OpenTable();
  void DeleteTable();
  virtual void PutKeyValue(yb::client::YBSession* session, string key, string value);
  void PutKeyValue(string key, string value);
  void ConfigureScanner(yb::client::YBScanner* scanner);
  void RestartCluster();
  void GetScanResults(yb::client::YBScanner* scanner,
                      vector<pair<string, string>>* result_kvs);
  void FetchTSMetricsPage();

  yb::client::sp::shared_ptr<yb::client::YBTable> table_;
  yb::client::sp::shared_ptr<yb::client::YBClient> client_;
  bool table_exists_ = false;

  yb::MiniCluster* mini_cluster() {
    assert(!use_external_mini_cluster());
    return mini_cluster_.get();
  }

  yb::ExternalMiniCluster* external_mini_cluster() {
    assert(use_external_mini_cluster());
    return external_mini_cluster_.get();
  }

  static constexpr int kDefaultNumMasters = 1;
  static constexpr int kDefaultNumTabletServers = 3;
  static constexpr int kDefaultSessionTimeoutMs = 60000;
  static constexpr int kDefaultClientRpcTimeoutMs = 30000;
  static constexpr bool kDefaultUsingExternalMiniCluster = false;
  static const char* const kDefaultTableName;

  vector<uint16_t> master_rpc_ports();
  void CreateClient();

  yb::client::sp::shared_ptr<yb::client::YBSession> NewSession();

  yb::client::YBSchema schema_;
  yb::client::sp::shared_ptr<yb::client::YBSession> session_;

  // Exactly one of the following two pointers will be set.
  std::unique_ptr<yb::MiniCluster> mini_cluster_;
  std::unique_ptr<yb::ExternalMiniCluster> external_mini_cluster_;
};

} // namespace integration_tests
} // namespace yb
#endif
