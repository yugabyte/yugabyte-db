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


#include "yb/client/xcluster_client.h"
#include "yb/client/yb_table_name.h"
#include "yb/common/entity_ids_types.h"
#include "yb/common/xcluster_util.h"
#include "yb/integration-tests/xcluster/xcluster_ddl_replication_test_base.h"
#include "yb/util/flags.h"
#include "yb/util/logging_test_util.h"

DECLARE_bool(TEST_simulate_EnsureSequenceUpdatesAreInWal_failure);
DECLARE_bool(ysql_enable_packed_row);
DECLARE_int32(TEST_xcluster_simulated_lag_ms);
DECLARE_int32(xcluster_ensure_sequence_updates_in_wal_timeout_sec);
DECLARE_int32(ysql_num_shards_per_tserver);

namespace yb {

class XClusterAutomaticModeTest : public XClusterDDLReplicationTestBase {
 public:
  Status SetUpClusters(
      bool use_different_database_oids, NamespaceName first_namespace,
      std::optional<NamespaceName> second_namespace = std::nullopt,
      bool start_yb_controller_servers = false) {
    // Set up first namespace.
    namespace_name = first_namespace;
    SetupParams params;
    params.replication_factor = 1;
    params.use_different_database_oids = use_different_database_oids;
    params.start_yb_controller_servers = start_yb_controller_servers;
    RETURN_NOT_OK(XClusterYsqlTestBase::SetUpClusters(params));

    // Set up second namespace if requested.
    if (second_namespace) {
      RETURN_NOT_OK(RunOnBothClusters([&](Cluster* cluster) -> Status {
        RETURN_NOT_OK(CreateDatabase(cluster, *second_namespace, params.is_colocated));
        auto table_name = VERIFY_RESULT(CreateYsqlTable(
            cluster, *second_namespace, "" /* schema_name */, "gratuitous_table",
            /*tablegroup_name=*/boost::none, /*num_tablets=*/1));

        std::shared_ptr<client::YBTable> table;
        RETURN_NOT_OK(cluster->client_->OpenTable(table_name, &table));
        cluster->tables_.emplace_back(std::move(table));

        return Status::OK();
      }));
    }

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_docdb_log_write_batches) = true;
    return Status::OK();
  }

  Status SetUpSequences(Cluster* cluster, const NamespaceName& namespace_name) {
    // Here we set up the sequences so they each have the same OID on both the source and target
    // universes.  We need this because our tests here do not actually do bootstrapping -- aka,
    // backup and restore --  so we have to make the OIDs the same ourselves manually.
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(namespace_name));
    RETURN_NOT_OK(conn.Execute(
        "SET yb_binary_restore = true;"
        "SET yb_ignore_pg_class_oids = false;"
        "SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('100000'::pg_catalog.oid);"
        "SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('100001'::pg_catalog.oid);"
        "CREATE SEQUENCE sequence_foo START 177777 CACHE 1 INCREMENT BY 42"));

    RETURN_NOT_OK(conn.Execute(
        "SET yb_binary_restore = true;"
        "SET yb_ignore_pg_class_oids = false;"
        "SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('200000'::pg_catalog.oid);"
        "SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('200001'::pg_catalog.oid);"
        "CREATE SEQUENCE sequence_bar START 277777 CACHE 1 INCREMENT BY 399"));

    return Status::OK();
  }

  Result<std::string> ReadSequences(Cluster* cluster, const NamespaceName& namespace_name) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(namespace_name));
    return Format(
        "foo: $0, bar: $1",
        VERIFY_RESULT(conn.FetchRowAsString("SELECT last_value, is_called FROM sequence_foo")),
        VERIFY_RESULT(conn.FetchRowAsString("SELECT last_value, is_called FROM sequence_bar")));
  }

  Status BumpSequences(Cluster* cluster, NamespaceName namespace_name) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(namespace_name));
    RETURN_NOT_OK(conn.FetchRowAsString("SELECT nextval('sequence_foo');"));
    RETURN_NOT_OK(conn.FetchRowAsString("SELECT nextval('sequence_bar');"));
    RETURN_NOT_OK(conn.FetchRowAsString("SELECT nextval('sequence_bar');"));
    return Status::OK();
  }

  Status WaitForSequencesReplicationDrain(std::vector<NamespaceId> namespace_names) {
    std::vector<NamespaceId> sequence_alias_ids;
    for (const auto& ns : namespace_names) {
      sequence_alias_ids.push_back(xcluster::GetSequencesDataAliasForNamespace(
          VERIFY_RESULT(GetNamespaceId(producer_client(), ns))));
    }
    return WaitForReplicationDrain(
        0, kRpcTimeout, /*target_time=*/std::nullopt, sequence_alias_ids);
  }

  Status VerifySequencesSameOnBothSides(NamespaceName namespace_name) {
    auto producer_side = VERIFY_RESULT(ReadSequences(&producer_cluster_, namespace_name));
    auto consumer_side = VERIFY_RESULT(ReadSequences(&consumer_cluster_, namespace_name));
    EXPECT_EQ(producer_side, consumer_side) << "checking namespace " << namespace_name;
    return Status::OK();
  }
};

TEST_F(XClusterAutomaticModeTest, StraightforwardSequenceReplication) {
  // Make sure we correctly wait for replications to drain in order to avoid flakiness.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulated_lag_ms) = 2000;

  // In this test, we just check if sequence bumps replicate.
  // We do not require transforming or filtering database OIDs.

  const std::string namespace1{"yugabyte"};
  ASSERT_OK(SetUpClusters(/*use_different_database_oids=*/false, namespace1));
  ASSERT_EQ(
      ASSERT_RESULT(GetNamespaceId(producer_client(), namespace1)),
      ASSERT_RESULT(GetNamespaceId(consumer_client(), namespace1)));

  ASSERT_OK(SetUpSequences(&producer_cluster_, namespace1));
  ASSERT_OK(SetUpSequences(&consumer_cluster_, namespace1));
  ASSERT_OK(VerifySequencesSameOnBothSides(namespace1));

  ASSERT_OK(CheckpointReplicationGroupOnNamespaces({namespace1}));
  ASSERT_OK(CreateReplicationFromCheckpoint());
  ASSERT_OK(VerifySequencesSameOnBothSides(namespace1));

  ASSERT_OK(BumpSequences(&producer_cluster_, namespace1));
  ASSERT_OK(WaitForSequencesReplicationDrain({namespace1}));
  ASSERT_OK(VerifySequencesSameOnBothSides(namespace1));

  ASSERT_OK(BumpSequences(&producer_cluster_, namespace1));
  ASSERT_OK(WaitForSequencesReplicationDrain({namespace1}));
  ASSERT_OK(VerifySequencesSameOnBothSides(namespace1));
}

TEST_F(XClusterAutomaticModeTest, SequenceReplicationWithFiltering) {
  // Unpacked is a harder test case for this code.  With unpacked rows, a single update to a
  // sequence will generate multiple RocksDB key value pairs.  This is harder for the xCluster code
  // to deal with because the GetChanges code batches multiple changes to the same row together.  In
  // particular, the code paths are different for the first change to a row and later changes to
  // that same row.  By using unpacked here, we force both those code paths to be tested.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = false;

  const std::string namespace1{"yugabyte"};
  const std::string namespace2{"yugabyte2"};
  ASSERT_OK(SetUpClusters(/*use_different_database_oids=*/false, namespace1, namespace2));
  ASSERT_EQ(
      ASSERT_RESULT(GetNamespaceId(producer_client(), namespace1)),
      ASSERT_RESULT(GetNamespaceId(consumer_client(), namespace1)));
  ASSERT_EQ(
      ASSERT_RESULT(GetNamespaceId(producer_client(), namespace2)),
      ASSERT_RESULT(GetNamespaceId(consumer_client(), namespace2)));

  ASSERT_OK(SetUpSequences(&producer_cluster_, namespace1));
  ASSERT_OK(SetUpSequences(&consumer_cluster_, namespace1));
  ASSERT_OK(SetUpSequences(&producer_cluster_, namespace2));
  ASSERT_OK(SetUpSequences(&consumer_cluster_, namespace2));
  ASSERT_OK(VerifySequencesSameOnBothSides(namespace2));

  auto original_namespace1_consumer_sequences =
      ASSERT_RESULT(ReadSequences(&consumer_cluster_, namespace1));

  // We are only going to replicate namespace2, which should leave
  // namespace1 on the consumer unchanged when we bump sequences of
  // namespace1 on the producer.
  std::vector<NamespaceName> namespaces_to_replicate = {namespace2};
  ASSERT_OK(CheckpointReplicationGroupOnNamespaces(namespaces_to_replicate));
  ASSERT_OK(CreateReplicationFromCheckpoint({}, kReplicationGroupId, namespaces_to_replicate));
  EXPECT_EQ(
      original_namespace1_consumer_sequences,
      ASSERT_RESULT(ReadSequences(&consumer_cluster_, namespace1)));

  ASSERT_OK(BumpSequences(&producer_cluster_, namespace2));
  ASSERT_OK(WaitForSequencesReplicationDrain(namespaces_to_replicate));
  ASSERT_OK(VerifySequencesSameOnBothSides(namespace2));
  EXPECT_EQ(
      original_namespace1_consumer_sequences,
      ASSERT_RESULT(ReadSequences(&consumer_cluster_, namespace1)));

  ASSERT_OK(BumpSequences(&producer_cluster_, namespace1));
  ASSERT_OK(WaitForSequencesReplicationDrain(namespaces_to_replicate));
  EXPECT_EQ(
      original_namespace1_consumer_sequences,
      ASSERT_RESULT(ReadSequences(&consumer_cluster_, namespace1)));
}

TEST_F(XClusterAutomaticModeTest, SequenceReplicationWithTwoDbs) {
  const std::string namespace1{"yugabyte"};
  const std::string namespace2{"yugabyte2"};
  ASSERT_OK(SetUpClusters(/*use_different_database_oids=*/false, namespace1, namespace2));

  ASSERT_OK(SetUpSequences(&producer_cluster_, namespace1));
  ASSERT_OK(SetUpSequences(&consumer_cluster_, namespace1));
  ASSERT_OK(SetUpSequences(&producer_cluster_, namespace2));
  ASSERT_OK(SetUpSequences(&consumer_cluster_, namespace2));

  std::vector<NamespaceName> namespaces_to_replicate = {namespace1, namespace2};
  {
    SCOPED_TRACE("Setting up replication");
    ASSERT_OK(CheckpointReplicationGroupOnNamespaces(namespaces_to_replicate));
    ASSERT_OK(CreateReplicationFromCheckpoint({}, kReplicationGroupId, namespaces_to_replicate));
    ASSERT_OK(VerifySequencesSameOnBothSides(namespace1));
    ASSERT_OK(VerifySequencesSameOnBothSides(namespace2));
  }

  {
    SCOPED_TRACE("Bumping first time");
    ASSERT_OK(BumpSequences(&producer_cluster_, namespace1));
    ASSERT_OK(WaitForSequencesReplicationDrain(namespaces_to_replicate));
    ASSERT_OK(VerifySequencesSameOnBothSides(namespace1));
    ASSERT_OK(VerifySequencesSameOnBothSides(namespace2));
  }

  {
    SCOPED_TRACE("Bumping second time");
    ASSERT_OK(BumpSequences(&producer_cluster_, namespace2));
    ASSERT_OK(WaitForSequencesReplicationDrain(namespaces_to_replicate));
    ASSERT_OK(VerifySequencesSameOnBothSides(namespace1));
    ASSERT_OK(VerifySequencesSameOnBothSides(namespace2));
  }
}

TEST_F(XClusterAutomaticModeTest, SequenceReplicationWithTransform) {
  // Make sequences_data use 5 tablets so routing between tablets gets exercised.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_num_shards_per_tserver) = 5;

  const std::string namespace1{"db_with_differing_oids"};
  ASSERT_OK(SetUpClusters(/*use_different_database_oids=*/true, namespace1));
  ASSERT_NE(
      ASSERT_RESULT(GetNamespaceId(producer_client(), namespace1)),
      ASSERT_RESULT(GetNamespaceId(consumer_client(), namespace1)));

  ASSERT_OK(SetUpSequences(&producer_cluster_, namespace1));
  ASSERT_OK(SetUpSequences(&consumer_cluster_, namespace1));
  ASSERT_OK(VerifySequencesSameOnBothSides(namespace1));

  std::vector<NamespaceName> namespaces_to_replicate = {namespace1};
  ASSERT_OK(CheckpointReplicationGroupOnNamespaces(namespaces_to_replicate));
  ASSERT_OK(CreateReplicationFromCheckpoint({}, kReplicationGroupId, namespaces_to_replicate));
  ASSERT_OK(VerifySequencesSameOnBothSides(namespace1));

  ASSERT_OK(BumpSequences(&producer_cluster_, namespace1));
  ASSERT_OK(WaitForSequencesReplicationDrain(namespaces_to_replicate));
  ASSERT_OK(VerifySequencesSameOnBothSides(namespace1));
}

TEST_F(XClusterAutomaticModeTest, SequenceSafeTime) {
  const std::string namespace1{"yugabyte"};
  ASSERT_OK(SetUpClusters(/*use_different_database_oids=*/false, namespace1));

  ASSERT_OK(SetUpSequences(&producer_cluster_, namespace1));
  ASSERT_OK(SetUpSequences(&consumer_cluster_, namespace1));

  std::vector<NamespaceName> namespaces_to_replicate = {namespace1};
  ASSERT_OK(CheckpointReplicationGroupOnNamespaces(namespaces_to_replicate));
  ASSERT_OK(CreateReplicationFromCheckpoint({}, kReplicationGroupId, namespaces_to_replicate));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow(namespaces_to_replicate));
  ASSERT_OK(VerifySequencesSameOnBothSides(namespace1));

  ASSERT_OK(BumpSequences(&producer_cluster_, namespace1));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow(namespaces_to_replicate));
  ASSERT_OK(VerifySequencesSameOnBothSides(namespace1));

  ASSERT_OK(BumpSequences(&producer_cluster_, namespace1));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow(namespaces_to_replicate));
  ASSERT_OK(VerifySequencesSameOnBothSides(namespace1));
}

TEST_F(XClusterAutomaticModeTest, SequencePausingAndSafeTime) {
  const std::string namespace1{"yugabyte"};
  ASSERT_OK(SetUpClusters(/*use_different_database_oids=*/false, namespace1));

  ASSERT_OK(SetUpSequences(&producer_cluster_, namespace1));
  ASSERT_OK(SetUpSequences(&consumer_cluster_, namespace1));

  std::vector<NamespaceName> namespaces_to_replicate = {namespace1};
  ASSERT_OK(CheckpointReplicationGroupOnNamespaces(namespaces_to_replicate));
  ASSERT_OK(CreateReplicationFromCheckpoint({}, kReplicationGroupId, namespaces_to_replicate));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow(namespaces_to_replicate));

  auto sequences_stream_id =
      ASSERT_RESULT(GetCDCStreamID(xcluster::GetSequencesDataAliasForNamespace(
          ASSERT_RESULT(GetNamespaceId(producer_client(), namespace_name)))));
  ASSERT_OK(PauseResumeXClusterProducerStreams({sequences_stream_id}, /*is_paused=*/true));
  ASSERT_OK(
      StringWaiterLogSink("Replication is paused from the producer for stream").WaitFor(300s));
  ASSERT_NOK(WaitForSafeTimeToAdvanceToNow(namespaces_to_replicate));

  ASSERT_OK(PauseResumeXClusterProducerStreams({sequences_stream_id}, /*is_paused=*/false));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow(namespaces_to_replicate));
}

TEST_F(XClusterAutomaticModeTest, SequencePausingIsolation) {
  const std::string namespace1{"yugabyte"};
  const std::string namespace2{"yugabyte2"};
  ASSERT_OK(SetUpClusters(/*use_different_database_oids=*/false, namespace1, namespace2));

  ASSERT_OK(RunOnBothClusters([&](Cluster* cluster) -> Status {
    RETURN_NOT_OK(SetUpSequences(cluster, namespace1));
    return SetUpSequences(cluster, namespace2);
  }));

  std::vector<NamespaceName> namespaces_to_replicate = {namespace1, namespace2};
  ASSERT_OK(CheckpointReplicationGroupOnNamespaces(namespaces_to_replicate));
  ASSERT_OK(CreateReplicationFromCheckpoint({}, kReplicationGroupId, namespaces_to_replicate));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow(namespaces_to_replicate));

  auto pause_one_namespace_temporarily = [&](NamespaceName namespace_to_pause,
                                             NamespaceName other_namespace) {
    auto namespace_to_pause_id =
        ASSERT_RESULT(GetNamespaceId(producer_client(), namespace_to_pause));
    LOG(INFO) << "***** Pausing namespace: " << namespace_to_pause
              << " ID: " << namespace_to_pause_id;
    auto sequences_stream_id = ASSERT_RESULT(
        GetCDCStreamID(xcluster::GetSequencesDataAliasForNamespace(namespace_to_pause_id)));
    ASSERT_OK(PauseResumeXClusterProducerStreams({sequences_stream_id}, /*is_paused=*/true));
    ASSERT_OK(
        StringWaiterLogSink(
            "Replication is paused from the producer for stream: "s + AsString(sequences_stream_id))
            .WaitFor(300s));
    ASSERT_OK(BumpSequences(&producer_cluster_, namespace_to_pause));

    std::vector<NamespaceName> paused_namespaces = {namespace_to_pause};
    std::vector<NamespaceName> unpaused_namespaces = {other_namespace};
    ASSERT_NOK(WaitForSafeTimeToAdvanceToNow(paused_namespaces));
    ASSERT_OK(WaitForSafeTimeToAdvanceToNow(unpaused_namespaces));

    LOG(INFO) << "***** Unpausing namespace: " << namespace_to_pause
              << " ID: " << namespace_to_pause_id;
    ASSERT_OK(PauseResumeXClusterProducerStreams({sequences_stream_id}, /*is_paused=*/false));
    ASSERT_OK(WaitForSafeTimeToAdvanceToNow(paused_namespaces));
    ASSERT_OK(WaitForSafeTimeToAdvanceToNow(unpaused_namespaces));
  };

  pause_one_namespace_temporarily(namespace1, namespace2);
  pause_one_namespace_temporarily(namespace2, namespace1);
}

TEST_F(XClusterAutomaticModeTest, SequenceReplicationBootstrappingWithoutBumps) {
  if (!UseYbController()) {
    GTEST_SKIP() << "This test does not work with yb_backup.py";
  }

  const std::string namespace1{"yugabyte"};
  ASSERT_OK(SetUpClusters(
      /*use_different_database_oids=*/false, namespace1, /*second_namespace=*/std::nullopt,
      /*start_yb_controller_servers=*/true));

  ASSERT_OK(SetUpSequences(&producer_cluster_, namespace1));

  ASSERT_OK(CheckpointReplicationGroupOnNamespaces({namespace1}));
  ASSERT_OK(BackupFromProducer({namespace1}));
  ASSERT_OK(RestoreToConsumer({namespace1}));
  ASSERT_OK(CreateReplicationFromCheckpoint({}, kReplicationGroupId, {namespace1}));
  ASSERT_OK(VerifySequencesSameOnBothSides(namespace1));

  ASSERT_OK(BumpSequences(&producer_cluster_, namespace1));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
  ASSERT_OK(VerifySequencesSameOnBothSides(namespace1));
}

TEST_F(XClusterAutomaticModeTest, SequenceReplicationBootstrappingBumpInMiddle) {
  if (!UseYbController()) {
    GTEST_SKIP() << "This test does not work with yb_backup.py";
  }

  const std::string namespace1{"yugabyte"};
  ASSERT_OK(SetUpClusters(
      /*use_different_database_oids=*/false, namespace1, /*second_namespace=*/std::nullopt,
      /*start_yb_controller_servers=*/true));
  ASSERT_OK(SetUpSequences(&producer_cluster_, namespace1));

  ASSERT_OK(CheckpointReplicationGroupOnNamespaces({namespace1}));
  ASSERT_OK(BackupFromProducer({namespace1}));

  ASSERT_OK(BumpSequences(&producer_cluster_, namespace1));

  ASSERT_OK(RestoreToConsumer({namespace1}));
  ASSERT_OK(CreateReplicationFromCheckpoint({}, kReplicationGroupId, {namespace1}));
  ASSERT_OK(VerifySequencesSameOnBothSides(namespace1));

  ASSERT_OK(BumpSequences(&producer_cluster_, namespace1));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
  ASSERT_OK(VerifySequencesSameOnBothSides(namespace1));
}

TEST_F(XClusterAutomaticModeTest, SequenceReplicationBootstrappingWith2Databases) {
  if (!UseYbController()) {
    GTEST_SKIP() << "This test does not work with yb_backup.py";
  }

  const std::string namespace1{"yugabyte"};
  const std::string namespace2{"yugabyte2"};
  ASSERT_OK(SetUpClusters(
      /*use_different_database_oids=*/false, namespace1, namespace2,
      /*start_yb_controller_servers=*/true));
  ASSERT_OK(SetUpSequences(&producer_cluster_, namespace1));
  ASSERT_OK(BumpSequences(&producer_cluster_, namespace1));
  ASSERT_OK(SetUpSequences(&producer_cluster_, namespace2));

  ASSERT_OK(CheckpointReplicationGroupOnNamespaces({namespace1, namespace2}));
  ASSERT_OK(BackupFromProducer({namespace1, namespace2}));

  ASSERT_OK(BumpSequences(&producer_cluster_, namespace1));
  ASSERT_OK(BumpSequences(&producer_cluster_, namespace2));

  ASSERT_OK(RestoreToConsumer({namespace1, namespace2}));
  ASSERT_OK(CreateReplicationFromCheckpoint({}, kReplicationGroupId, {namespace1, namespace2}));

  ASSERT_OK(VerifySequencesSameOnBothSides(namespace1));
  ASSERT_OK(VerifySequencesSameOnBothSides(namespace2));
}

TEST_F(XClusterAutomaticModeTest, SequenceReplicationEnsureWalsFails) {
  if (!UseYbController()) {
    GTEST_SKIP() << "This test does not work with yb_backup.py";
  }

  // Make the EnsureSequenceUpdatesAreInWal call, which is part of bootstrapping, fail.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_EnsureSequenceUpdatesAreInWal_failure) = true;

  const std::string namespace1{"yugabyte"};
  ASSERT_OK(SetUpClusters(
      /*use_different_database_oids=*/false, namespace1, /*second_namespace=*/std::nullopt,
      /*start_yb_controller_servers=*/true));
  ASSERT_OK(SetUpSequences(&producer_cluster_, namespace1));

  ASSERT_OK(CheckpointReplicationGroupOnNamespaces({namespace1}));
}

}  // namespace yb
