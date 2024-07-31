// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#pragma once

#include <gtest/gtest.h>

#include "yb/common/wire_protocol.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/master/sys_catalog.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"

#include "yb/server/rpc_server.h"

#include "yb/util/net/sockaddr.h"
#include "yb/util/result.h"
#include "yb/util/status_fwd.h"
#include "yb/util/test_util.h"

namespace yb {
namespace master {

class SysCatalogTest : public YBTest {
 protected:
  void SetUp() override {
    YBTest::SetUp();

    // Start master with the create flag on.
    mini_master_.reset(
        new MiniMaster(Env::Default(), GetTestPath("Master"), AllocateFreePort(),
                       AllocateFreePort(), 0));
    ASSERT_OK(mini_master_->Start());
    SetLocalVars();
    rpc::MessengerBuilder builder("syscatalog-messenger");
    builder.set_num_reactors(1);
    messenger_ = ASSERT_RESULT(builder.Build());
    proxy_cache_ = std::make_unique<rpc::ProxyCache>(messenger_.get());
    ASSERT_OK(master_->WaitUntilCatalogManagerIsLeaderAndReadyForTests());
  }

  Status RestartMaster() {
    LOG(INFO) << "Restarting master";
    RETURN_NOT_OK(mini_master_->Restart());
    LOG(INFO) << "Restarted master";

    SetLocalVars();
    return Status::OK();
  }

  void TearDown() override {
    mini_master_->Shutdown();
    messenger_->Shutdown();
    YBTest::TearDown();
  }

  // Create a new TableInfo.
  TableInfoPtr CreateUncommittedTable(const std::string& table_id,
                           SysTablesEntryPB_State state = SysTablesEntryPB::PREPARING,
                           Schema schema = Schema(),
                           bool colocated = false) {
    auto table = master_->catalog_manager()->NewTableInfo(table_id, colocated);
    table->mutable_metadata()->StartMutation();
    auto* metadata = &table->mutable_metadata()->mutable_dirty()->pb;
    metadata->set_name("testtb");
    metadata->set_version(0);
    metadata->mutable_replication_info()->mutable_live_replicas()->set_num_replicas(1);
    metadata->set_state(state);
    SchemaToPB(schema, metadata->mutable_schema());
    return table;
  }

  // Create a new TabletInfo.
  TabletInfoPtr CreateUncommittedTablet(TableInfo *table,
                                        const std::string& tablet_id,
                                        const std::string& start_key = "",
                                        const std::string& end_key = "") {
    auto tablet = std::make_shared<TabletInfo>(table, tablet_id);
    tablet->mutable_metadata()->StartMutation();
    auto* metadata = &tablet->mutable_metadata()->mutable_dirty()->pb;
    metadata->set_state(SysTabletsEntryPB::PREPARING);
    metadata->mutable_partition()->set_partition_key_start(start_key);
    metadata->mutable_partition()->set_partition_key_end(end_key);
    metadata->set_table_id(table->id());
    metadata->add_table_ids(table->id());
    return tablet;
  }

  Result<ChangeMasterClusterConfigResponsePB> ChangeMasterClusterConfig(
      const ChangeMasterClusterConfigRequestPB& req) {
    rpc::RpcController rpc;
    ChangeMasterClusterConfigResponsePB resp;
    auto proxy = master::MasterClusterProxy(proxy_cache_.get(), mini_master_->bound_rpc_addr());
    RETURN_NOT_OK(proxy.ChangeMasterClusterConfig(req, &resp, &rpc));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    return resp;
  }

  std::unique_ptr<MiniMaster> mini_master_;
  Master* master_;
  SysCatalogTable* sys_catalog_;

 private:
  std::unique_ptr<rpc::Messenger> messenger_;
  std::unique_ptr<rpc::ProxyCache> proxy_cache_;

  void SetLocalVars() {
    master_ = mini_master_->master();
    ASSERT_OK(master_->WaitUntilCatalogManagerIsLeaderAndReadyForTests());
    sys_catalog_ = master_->catalog_manager()->sys_catalog();
  }
};

const int64_t kLeaderTerm = 1;

inline bool PbEquals(const google::protobuf::Message& a, const google::protobuf::Message& b) {
  return a.DebugString() == b.DebugString();
}

template<class C>
std::pair<std::string, std::string> AssertMetadataEqualsHelper(C* ti_a, C* ti_b) {
  auto l_a = ti_a->LockForRead();
  auto l_b = ti_b->LockForRead();
  return std::make_pair(l_a->pb.DebugString(), l_b->pb.DebugString());
}

// Similar to ASSERT_EQ but compares string representations of protobufs stored in two system
// catalog metadata objects.
//
// This uses a gtest internal macro for user-friendly output, as it shows the expressions passed to
// ASSERT_METADATA_EQ the same way ASSERT_EQ would show them. If the internal macro stops working
// the way it does now with a future version of gtest, it could be replaced with:
//
// ASSERT_EQ(string_reps.first, string_reps.second)
//     << "Expecting string representations of metadata protobufs to be the same for "
//     << #a << " and " << #b;

#define ASSERT_METADATA_EQ(a, b) do { \
    auto string_reps = AssertMetadataEqualsHelper((a), (b)); \
    GTEST_ASSERT_( \
      ::testing::internal::EqHelper::Compare \
          (#a, #b, string_reps.first, string_reps.second), \
          GTEST_FATAL_FAILURE_); \
  } while (false)

class TestTableLoader : public Visitor<PersistentTableInfo> {
 public:
  TestTableLoader() {}
  ~TestTableLoader() { Reset(); }

  void Reset() {
    for (const auto& entry :  tables) {
      entry.second->Release();
    }
    tables.clear();
  }

  Status Visit(const std::string& table_id, const SysTablesEntryPB& metadata) override {
    // Setup the table info
    TableInfo *table = new TableInfo(table_id, /* colocated */ false);
    auto l = table->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);
    l.Commit();
    table->AddRef();
    tables[table->id()] = table;
    return Status::OK();
  }

  std::map<std::string, TableInfo*> tables;
};

class TestTabletLoader : public Visitor<PersistentTabletInfo> {
 public:
  TestTabletLoader() {}
  ~TestTabletLoader() { Reset(); }

  void Reset() {
    tablets.clear();
  }

  Status Visit(const std::string& tablet_id, const SysTabletsEntryPB& metadata) override {
    // Setup the tablet info
    auto tablet = std::make_shared<TabletInfo>(nullptr, tablet_id);
    auto l = tablet->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);
    l.Commit();
    tablets[tablet->id()] = std::move(tablet);
    return Status::OK();
  }

  std::map<std::string, std::shared_ptr<TabletInfo>> tablets;
};

class TestClusterConfigLoader : public Visitor<PersistentClusterConfigInfo> {
 public:
  TestClusterConfigLoader() {}
  ~TestClusterConfigLoader() { Reset(); }

  virtual Status Visit(
      const std::string& fake_id, const SysClusterConfigEntryPB& metadata) override {
    CHECK(!config_info) << "We either got multiple config_info entries, or we didn't Reset()";
    config_info = std::make_shared<ClusterConfigInfo>();
    auto l = config_info->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);
    l.Commit();
    return Status::OK();
  }

  void Reset() {
    config_info.reset();
  }

  std::shared_ptr<ClusterConfigInfo> config_info = nullptr;
};

class TestNamespaceLoader : public Visitor<PersistentNamespaceInfo> {
 public:
  TestNamespaceLoader() {}
  ~TestNamespaceLoader() { Reset(); }

  void Reset() {
    for (NamespaceInfo* ni : namespaces) {
      ni->Release();
    }
    namespaces.clear();
  }

  Status Visit(const std::string& ns_id, const SysNamespaceEntryPB& metadata) override {
    // Setup the namespace info
    NamespaceInfo* const ns = new NamespaceInfo(ns_id, /*tasks_tracker=*/nullptr);
    auto l = ns->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);
    l.Commit();
    ns->AddRef();
    namespaces.push_back(ns);
    return Status::OK();
  }

  std::vector<NamespaceInfo*> namespaces;
};

class TestUDTypeLoader : public Visitor<PersistentUDTypeInfo> {
 public:
  TestUDTypeLoader() {}
  ~TestUDTypeLoader() { Reset(); }

  void Reset() {
    for (UDTypeInfo* tp : udtypes) {
      tp->Release();
    }
    udtypes.clear();
  }

  Status Visit(const std::string& udtype_id, const SysUDTypeEntryPB& metadata) override {
    // Setup the udtype info
    UDTypeInfo* const tp = new UDTypeInfo(udtype_id);
    auto l = tp->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);
    l.Commit();
    tp->AddRef();
    udtypes.push_back(tp);
    return Status::OK();
  }

  std::vector<UDTypeInfo*> udtypes;
};

class TestRedisConfigLoader : public Visitor<PersistentRedisConfigInfo> {
 public:
  TestRedisConfigLoader() {}
  ~TestRedisConfigLoader() { Reset(); }

  void Reset() {
    for (RedisConfigInfo* rci : config_entries) {
      rci->Release();
    }
    config_entries.clear();
  }

  Status Visit(const std::string& key, const SysRedisConfigEntryPB& metadata) override {
    // Setup the redis config info
    RedisConfigInfo* const rci = new RedisConfigInfo(key);
    auto l = rci->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);
    l.Commit();
    rci->AddRef();
    config_entries.push_back(rci);
    return Status::OK();
  }

  std::vector<RedisConfigInfo*> config_entries;
};

class TestSysConfigLoader : public Visitor<PersistentSysConfigInfo> {
 public:
  TestSysConfigLoader() {}
  ~TestSysConfigLoader() { Reset(); }

  void Reset() {
    for (SysConfigInfo* sys_config : sys_configs) {
      sys_config->Release();
    }
    sys_configs.clear();
  }

  Status Visit(const std::string& id, const SysConfigEntryPB& metadata) override {

    // Setup the sysconfig info.
    SysConfigInfo* const sys_config = new SysConfigInfo(id /* config_type */);
    auto l = sys_config->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);
    l.Commit();
    sys_config->AddRef();
    sys_configs.push_back(sys_config);
    LOG(INFO) << " Current SysConfigInfo: " << sys_config->ToString();
    return Status::OK();
  }

  std::vector<SysConfigInfo*> sys_configs;
};

class TestRoleLoader : public Visitor<PersistentRoleInfo> {
 public:
  TestRoleLoader() {}
  ~TestRoleLoader() { Reset(); }

  void Reset() {
    for (RoleInfo* rl : roles) {
      rl->Release();
    }
    roles.clear();
  }

  Status Visit(const RoleName& role_name, const SysRoleEntryPB& metadata) override {

    // Setup the role info
    RoleInfo* const rl = new RoleInfo(role_name);
    auto l = rl->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);
    l.Commit();
    rl->AddRef();
    roles.push_back(rl);
    LOG(INFO) << " Current Role: " << rl->ToString();
    return Status::OK();
  }

  std::vector<RoleInfo*> roles;
};

} // namespace master
} // namespace yb
