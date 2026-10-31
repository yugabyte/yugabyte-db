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

#include <benchmark/benchmark.h>

#include <memory>
#include <string>

#include "yb/common/common_net.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/mini_master.h"

#include "yb/util/env.h"
#include "yb/util/format.h"
#include "yb/util/path_util.h"
#include "yb/util/port_picker.h"
#include "yb/util/result.h"
#include "yb/util/status_log.h"
#include "yb/util/uuid.h"

DECLARE_bool(enable_ysql);

namespace yb::master {
namespace {

// Measures only config access on an unchanged heartbeat, not the complete heartbeat handler.
class ClusterConfigBenchmark : public benchmark::Fixture {
 public:
  void SetUp(const benchmark::State& state) override {
    FLAGS_enable_ysql = false;
    auto* env = Env::Default();
    data_dir_ = JoinPathSegments(
        CHECK_RESULT(env->GetTestDirectory()),
        "cluster-config-benchmark-" + Uuid::Generate().ToString());
    master_ = std::make_unique<MiniMaster>(
        env, data_dir_, port_picker_.AllocateFreePort(), port_picker_.AllocateFreePort(), 0);
    CHECK_OK(master_->Start());
    CHECK_OK(master_->WaitUntilCatalogManagerIsLeaderAndReadyForTests());
    catalog_manager_ = &master_->catalog_manager_impl();

    auto l = catalog_manager_->ClusterConfig()->LockForWrite();
    auto& config = l.mutable_data()->pb;
    auto* placement = config.mutable_replication_info()->mutable_live_replicas();
    placement->set_num_replicas(3);
    for (int i = 0; i < 3; ++i) {
      auto* block = placement->add_placement_blocks();
      block->set_min_num_replicas(1);
      block->mutable_cloud_info()->set_placement_cloud("cloud");
      block->mutable_cloud_info()->set_placement_region("region");
      block->mutable_cloud_info()->set_placement_zone(Format("zone-$0", i));
    }
    for (int i = 0; i < 6; ++i) {
      auto host = MakeHostPortPB(Format("server-$0.example.com", i), 9100);
      *config.mutable_server_blacklist()->add_hosts() = host;
      *config.mutable_leader_blacklist()->add_hosts() = host;
    }
    if (state.range(0)) {
      auto& producer =
          (*config.mutable_consumer_registry()->mutable_producer_map())["replication-group"];
      producer.set_disable_stream(true);
      auto& stream = (*producer.mutable_stream_map())["stream-1"];
      stream.set_consumer_table_id(Uuid::Generate().ToHexString());
      stream.set_producer_table_id(Uuid::Generate().ToHexString());
      for (int64_t i = 0; i < state.range(0); ++i) {
        auto& tablets =
            (*stream.mutable_consumer_producer_tablet_map())[Uuid::Generate().ToHexString()];
        tablets.add_tablets(Uuid::Generate().ToHexString());
        tablets.add_start_key(Format("key-$0", i));
        tablets.add_end_key(Format("key-$0", i + 1));
      }
    }
    config_bytes_ = config.ByteSizeLong();
    l.Commit();
  }

  void TearDown(const benchmark::State&) override {
    master_->Shutdown();
    master_.reset();
    CHECK_OK(Env::Default()->DeleteRecursively(data_dir_));
  }

 protected:
  CatalogManager* catalog_manager_ = nullptr;
  size_t config_bytes_ = 0;

 private:
  PortPicker port_picker_;
  std::string data_dir_;
  std::unique_ptr<MiniMaster> master_;
};

BENCHMARK_DEFINE_F(ClusterConfigBenchmark, FullCopies)(benchmark::State& state) {
  for (auto _ : state) {
    for (int i = 0; i < 3; ++i) {
      auto config = CHECK_RESULT(catalog_manager_->GetClusterConfig());
      benchmark::DoNotOptimize(config);
    }
  }
  state.counters["config_bytes"] = config_bytes_;
}

BENCHMARK_DEFINE_F(ClusterConfigBenchmark, NarrowReads)(benchmark::State& state) {
  for (auto _ : state) {
    auto uuids = CHECK_RESULT(catalog_manager_->GetClusterConfigUuids());
    benchmark::DoNotOptimize(uuids);
    benchmark::DoNotOptimize(CHECK_RESULT(catalog_manager_->GetOidCacheInvalidationsCount()));
    benchmark::DoNotOptimize(CHECK_RESULT(catalog_manager_->GetClusterConfigVersion()));
  }
  state.counters["config_bytes"] = config_bytes_;
}

BENCHMARK_REGISTER_F(ClusterConfigBenchmark, FullCopies)->Arg(0)->Arg(1000)->Arg(10000);
BENCHMARK_REGISTER_F(ClusterConfigBenchmark, NarrowReads)->Arg(0)->Arg(1000)->Arg(10000);

}  // namespace
}  // namespace yb::master
