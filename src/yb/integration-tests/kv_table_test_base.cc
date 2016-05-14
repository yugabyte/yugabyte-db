// Copyright (c) YugaByte, Inc.

#include "yb/integration-tests/kv_table_test_base.h"

using std::unique_ptr;
using yb::client::sp::shared_ptr;

namespace yb {

using client::YBClient;
using client::YBClientBuilder;
using client::YBColumnSchema;
using client::YBInsert;
using client::YBScanner;
using client::YBScanBatch;
using client::YBSchemaBuilder;
using client::YBSession;
using client::YBTableCreator;
using client::YBTableType;

namespace integration_tests {

const char* const KVTableTestBase::kDefaultTableName = "kv-table-test";

int KVTableTestBase::num_masters() {
  return kDefaultNumMasters;
}

int KVTableTestBase::num_tablet_servers() {
  return kDefaultNumTabletServers;
}

int KVTableTestBase::session_timeout_ms() {
  return kDefaultSessionTimeoutMs;
}

string KVTableTestBase::table_name() {
  return kDefaultTableName;
}

int KVTableTestBase::client_rpc_timeout_ms() {
  return kDefaultClientRpcTimeoutMs;
}

bool KVTableTestBase::use_external_mini_cluster() {
  return kDefaultUsingExternalMiniCluster;
}

KVTableTestBase::KVTableTestBase() {
}

void KVTableTestBase::SetUp() {
  YBTest::SetUp();
  if (use_external_mini_cluster()) {
    auto opts = ExternalMiniClusterOptions();
    opts.num_masters = num_masters();
    opts.master_rpc_ports = master_rpc_ports();
    opts.num_tablet_servers = num_tablet_servers();

    external_mini_cluster_.reset(new ExternalMiniCluster(opts));
    ASSERT_OK(external_mini_cluster_->Start());
  } else {
    auto opts = MiniClusterOptions();
    opts.num_masters = num_masters();
    opts.master_rpc_ports = master_rpc_ports();
    opts.num_tablet_servers = num_tablet_servers();

    mini_cluster_.reset(new MiniCluster(env_.get(), opts));
    ASSERT_OK(mini_cluster_->Start());
  }

  CreateClient();

  unique_ptr<YBTableCreator> table_creator(client_->NewTableCreator());

  YBSchemaBuilder b;
  b.AddColumn("k")->Type(YBColumnSchema::BINARY)->NotNull()->PrimaryKey();
  b.AddColumn("v")->Type(YBColumnSchema::BINARY)->NotNull();
  ASSERT_OK(b.Build(&schema_));

  ASSERT_OK(table_creator->table_name(table_name())
      .table_type(YBTableType::KEY_VALUE_TABLE_TYPE)
      .num_replicas(3)
      .schema(&schema_)
      .Create());
  OpenTable();
}

void KVTableTestBase::TearDown() {
  if (use_external_mini_cluster()) {
    external_mini_cluster_->Shutdown();
  } else {
    mini_cluster_->Shutdown();
  }
  YBTest::TearDown();
}

vector<uint16_t> KVTableTestBase::master_rpc_ports() {
  vector<uint16_t> master_rpc_ports;
  for (int i = 0; i < num_masters(); ++i) {
    master_rpc_ports.push_back(0);
  }
  return master_rpc_ports;
}

void KVTableTestBase::CreateClient() {
  client_.reset();
  YBClientBuilder builder;
  builder.default_rpc_timeout(MonoDelta::FromMilliseconds(client_rpc_timeout_ms()));
  if (use_external_mini_cluster()) {
    ASSERT_OK(external_mini_cluster_->CreateClient(builder, &client_));
  } else {
    ASSERT_OK(mini_cluster_->CreateClient(&builder, &client_));
  }
}

void KVTableTestBase::OpenTable() {
  ASSERT_OK(client_->OpenTable(kDefaultTableName, &table_));
  ASSERT_EQ(YBTableType::KEY_VALUE_TABLE_TYPE, table_->table_type());
  session_ = NewSession();
}

shared_ptr<YBSession> KVTableTestBase::NewSession() {
  shared_ptr<YBSession> session = client_->NewSession();
  session->SetTimeoutMillis(session_timeout_ms());
  CHECK_OK(session->SetFlushMode(YBSession::MANUAL_FLUSH));
  return session;
}

void KVTableTestBase::PutKeyValue(YBSession* session, string key, string value) {
  unique_ptr<YBInsert> insert(table_->NewInsert());
  insert->mutable_row()->SetBinary("k", key);
  insert->mutable_row()->SetBinary("v", value);
  ASSERT_OK(session->Apply(insert.release()));
  ASSERT_OK(session->Flush());
}

void KVTableTestBase::PutKeyValue(string key, string value) {
  PutKeyValue(session_.get(), key, value);
}

void KVTableTestBase::ConfigureScanner(YBScanner* scanner) {
  scanner->SetSelection(YBClient::ReplicaSelection::LEADER_ONLY);
  ASSERT_OK(scanner->SetProjectedColumns({ "k", "v" }));
}

void KVTableTestBase::RestartCluster() {
  DCHECK(!use_external_mini_cluster());
  mini_cluster_->RestartSync();
  NO_FATALS(CreateClient());
  NO_FATALS(OpenTable());
}

void KVTableTestBase::GetScanResults(YBScanner* scanner, vector<pair<string, string>>* result_kvs) {
  while (scanner->HasMoreRows()) {
    vector<YBScanBatch::RowPtr> rows;
    scanner->NextBatch(&rows);
    for (auto row : rows) {
      Slice returned_key, returned_value;
      ASSERT_OK(row.GetBinary("k", &returned_key));
      ASSERT_OK(row.GetBinary("v", &returned_value));
      result_kvs->emplace_back(make_pair(returned_key.ToString(), returned_value.ToString()));
    }
  }
}


}; // namespace integration_tests
} // namespace yb
