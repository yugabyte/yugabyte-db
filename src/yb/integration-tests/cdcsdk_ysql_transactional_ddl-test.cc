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

#include <map>
#include <vector>

#include <gtest/gtest.h>

#include "yb/common/constants.h"
#include "yb/common/ddl_mode-test-util.h"
#include "yb/integration-tests/cdcsdk_ysql_test_base.h"
#include "yb/util/test_macros.h"
#include "yb/util/tostring.h"

namespace yb {
namespace cdc {

// A restart after partially streaming a transactional DDL txn drops the in-memory
// cached_schema_details. Unpacked intents carry no schema version, so the schema cached for the
// first remaining intent is reused for the rest of the txn and misses column ids written after a
// later ADD COLUMN; decoding then fails with "Column id ... not found".
TEST_F(CDCSDKYsqlTest, TestGrpcStreamTransactionalDdlGetChangesFailsAfterRestart) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replication_slot_transactional_ddl) = true;
  ToggleDDLMode(/* use_legacy = */ false);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_max_stream_intent_records) = 10;

  ASSERT_OK(SetUpWithParams(1, 1, false));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (a int primary key, b text, c int) SPLIT INTO 1 TABLETS"));
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, test_namespace_name, "t1"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), 1);

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot(
      "pg_grpc_slot", CDCSDKSnapshotOption::NOEXPORT_SNAPSHOT,
      /* verify_snapshot_name */ false, test_namespace_name, kYbGrpcStreamIndicator));

  ASSERT_OK(conn.Execute("BEGIN"));
  for (int i = 1; i <= 40; ++i) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO t1 VALUES ($0, 'v$0', $0)", i));
  }
  ASSERT_OK(conn.Execute("ALTER TABLE t1 ADD COLUMN d text"));
  for (int i = 41; i <= 80; ++i) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO t1 VALUES ($0, 'v$0', $0, 'd$0')", i));
  }
  ASSERT_OK(conn.Execute("ALTER TABLE t1 DROP COLUMN c"));
  for (int i = 81; i <= 120; ++i) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO t1 VALUES ($0, 'v$0', 'd$0')", i));
  }
  ASSERT_OK(conn.Execute("COMMIT"));

  auto find_col = [](const RowMessage& row, const std::string& name) -> const DatumMessagePB* {
    for (const auto& col : row.new_tuple()) {
      if (col.column_name() == name) {
        return &col;
      }
    }
    return nullptr;
  };
  auto col_has_value = [](const DatumMessagePB* col) {
    return col != nullptr && col->datum_case() != DatumMessagePB::DATUM_NOT_SET &&
           !col->has_datum_missing();
  };

  std::map<int32_t, RowMessage> inserts;
  auto collect_inserts = [&](const GetChangesResponsePB& resp) {
    for (const auto& record : resp.cdc_sdk_proto_records()) {
      const auto& row = record.row_message();
      if (row.op() != RowMessage::INSERT || row.table() != "t1") {
        continue;
      }
      const auto* a = find_col(row, "a");
      if (col_has_value(a)) {
        inserts[a->datum_int32()] = row;
      }
    }
  };

  auto change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
  ASSERT_FALSE(change_resp.cdc_sdk_checkpoint().key().empty());
  ASSERT_NE(change_resp.cdc_sdk_checkpoint().write_id(), 0);
  collect_inserts(change_resp);
  // The intent batch is smaller than the txn, so the restart happens before every schema version
  // has been streamed.
  ASSERT_LT(inserts.size(), 120);

  auto checkpoint = change_resp.cdc_sdk_checkpoint();

  auto* tserver = test_cluster()->mini_tablet_server(0);
  ASSERT_OK(tserver->Restart());
  ASSERT_OK(tserver->WaitStarted());

  bool streamed_after_restart = false;
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto resp = GetChangesFromCDCWithoutRetry(stream_id, tablets, &checkpoint);
        if (!resp.ok()) {
          if (!streamed_after_restart &&
              (resp.status().IsLeaderNotReadyToServe() || resp.status().IsNotFound())) {
            return false;
          }
          return resp.status();
        }
        streamed_after_restart = true;
        change_resp = *resp;
        collect_inserts(change_resp);
        checkpoint = change_resp.cdc_sdk_checkpoint();
        LOG(INFO) << "Post-restart GetChanges records=" << change_resp.cdc_sdk_proto_records_size()
                  << " inserts_so_far=" << inserts.size()
                  << " key_empty=" << checkpoint.key().empty()
                  << " write_id=" << checkpoint.write_id();
        return checkpoint.key().empty() && checkpoint.write_id() == 0;
      },
      MonoDelta::FromSeconds(60) * kTimeMultiplier,
      "Waiting to finish streaming the transactional DDL txn after tserver restart"));

  ASSERT_EQ(inserts.size(), 120);
  for (int i = 1; i <= 120; ++i) {
    auto it = inserts.find(i);
    ASSERT_NE(it, inserts.end()) << i;
    const auto& row = it->second;
    const auto* a = find_col(row, "a");
    const auto* b = find_col(row, "b");
    const auto* c = find_col(row, "c");
    const auto* d = find_col(row, "d");
    ASSERT_TRUE(col_has_value(a));
    ASSERT_TRUE(col_has_value(b));
    ASSERT_EQ(a->datum_int32(), i);
    ASSERT_EQ(b->datum_string(), Format("v$0", i));
    if (i <= 80) {
      ASSERT_TRUE(col_has_value(c)) << i;
      ASSERT_EQ(c->datum_int32(), i);
    } else {
      ASSERT_FALSE(col_has_value(c)) << i;
    }
    if (i >= 41) {
      ASSERT_TRUE(col_has_value(d)) << i;
      ASSERT_EQ(d->datum_string(), Format("d$0", i));
    } else {
      ASSERT_FALSE(col_has_value(d)) << i;
    }
  }
}

// One transaction writes rows before ADD COLUMN, while the new column exists, after DROP COLUMN,
// and after the same column is added again with a different type. Streaming must decode each
// insert with the columns that existed when it was written.
TEST_F(CDCSDKYsqlTest, TestGrpcStreamTransactionalDdlAddAndDropColumn) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replication_slot_transactional_ddl) = true;
  ToggleDDLMode(/* use_legacy = */ false);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = false;

  ASSERT_OK(SetUpWithParams(1, 1, false));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (a int primary key, b text) SPLIT INTO 1 TABLETS"));
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, test_namespace_name, "t1"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), 1);

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot(
      "pg_grpc_slot", CDCSDKSnapshotOption::NOEXPORT_SNAPSHOT,
      /* verify_snapshot_name */ false, test_namespace_name, kYbGrpcStreamIndicator));

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (1, 'before')"));
  ASSERT_OK(conn.Execute("ALTER TABLE t1 ADD COLUMN c int"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (2, 'added', 20)"));
  ASSERT_OK(conn.Execute("ALTER TABLE t1 DROP COLUMN c"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (3, 'dropped')"));
  ASSERT_OK(conn.Execute("ALTER TABLE t1 ADD COLUMN c text"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (4, 'readded', 'text-c')"));
  ASSERT_OK(conn.Execute("COMMIT"));

  auto find_col = [](const RowMessage& row, const std::string& name) -> const DatumMessagePB* {
    for (const auto& col : row.new_tuple()) {
      if (col.column_name() == name) {
        return &col;
      }
    }
    return nullptr;
  };
  auto col_has_value = [](const DatumMessagePB* col) {
    return col != nullptr && col->datum_case() != DatumMessagePB::DATUM_NOT_SET &&
           !col->has_datum_missing();
  };

  std::map<int32_t, RowMessage> inserts;
  auto collect_inserts = [&](const GetChangesResponsePB& resp) {
    for (const auto& record : resp.cdc_sdk_proto_records()) {
      const auto& row = record.row_message();
      if (row.op() != RowMessage::INSERT || row.table() != "t1") {
        continue;
      }
      const auto* a = find_col(row, "a");
      if (col_has_value(a)) {
        inserts[a->datum_int32()] = row;
      }
    }
  };

  CDCSDKCheckpointPB checkpoint;
  bool streamed = false;
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto resp = streamed ? GetChangesFromCDCWithoutRetry(stream_id, tablets, &checkpoint)
                             : GetChangesFromCDC(stream_id, tablets, &checkpoint);
        if (!resp.ok()) {
          if (!streamed &&
              (resp.status().IsLeaderNotReadyToServe() || resp.status().IsNotFound())) {
            return false;
          }
          return resp.status();
        }
        streamed = true;
        collect_inserts(*resp);
        checkpoint = resp->cdc_sdk_checkpoint();
        LOG(INFO) << "GetChanges records=" << resp->cdc_sdk_proto_records_size()
                  << " inserts_so_far=" << inserts.size()
                  << " key_empty=" << checkpoint.key().empty()
                  << " write_id=" << checkpoint.write_id();
        return inserts.size() == 4 && checkpoint.key().empty() && checkpoint.write_id() == 0;
      },
      MonoDelta::FromSeconds(60) * kTimeMultiplier,
      "Waiting to stream the transactional DDL txn"));

  ASSERT_EQ(inserts.size(), 4);
  const auto& before = inserts.at(1);
  const auto& added = inserts.at(2);
  const auto& dropped = inserts.at(3);
  const auto& readded = inserts.at(4);

  ASSERT_FALSE(before.transaction_id().empty());
  ASSERT_EQ(before.transaction_id(), added.transaction_id());
  ASSERT_EQ(before.transaction_id(), dropped.transaction_id());
  ASSERT_EQ(before.transaction_id(), readded.transaction_id());

  ASSERT_EQ(find_col(before, "b")->datum_string(), "before");
  ASSERT_FALSE(col_has_value(find_col(before, "c")));

  ASSERT_EQ(find_col(added, "b")->datum_string(), "added");
  ASSERT_TRUE(col_has_value(find_col(added, "c")));
  ASSERT_EQ(find_col(added, "c")->datum_int32(), 20);

  ASSERT_EQ(find_col(dropped, "b")->datum_string(), "dropped");
  ASSERT_FALSE(col_has_value(find_col(dropped, "c")));

  ASSERT_EQ(find_col(readded, "b")->datum_string(), "readded");
  ASSERT_TRUE(col_has_value(find_col(readded, "c")));
  ASSERT_EQ(find_col(readded, "c")->datum_string(), "text-c");
}

// Packed rows with REPLICA IDENTITY FULL. A restart mid-APPLY must still decode each update in
// the transactional DDL txn, including columns added and dropped between those updates.
TEST_F(CDCSDKYsqlTest, TestGrpcStreamTransactionalDdlPackedFullGetChangesAfterRestart) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replication_slot_transactional_ddl) = true;
  ToggleDDLMode(/* use_legacy = */ false);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_enable_intra_transactional_before_image) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_max_stream_intent_records) = 10;

  ASSERT_OK(SetUpWithParams(1, 1, false));

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (a int primary key, b text, c int) SPLIT INTO 1 TABLETS"));
  ASSERT_OK(conn.Execute("ALTER TABLE t1 REPLICA IDENTITY FULL"));
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, test_namespace_name, "t1"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, /* partition_list_version =*/nullptr));
  ASSERT_EQ(tablets.size(), 1);

  auto stream_id = ASSERT_RESULT(CreateConsistentSnapshotStreamWithReplicationSlot(
      "pg_grpc_slot", CDCSDKSnapshotOption::NOEXPORT_SNAPSHOT,
      /* verify_snapshot_name */ false, test_namespace_name, kYbGrpcStreamIndicator));

  for (int i = 1; i <= 40; ++i) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO t1 VALUES ($0, 'v$0', $0)", i));
  }

  ASSERT_OK(conn.Execute("BEGIN"));
  for (int i = 1; i <= 40; ++i) {
    ASSERT_OK(conn.ExecuteFormat("UPDATE t1 SET b = 'u$0' WHERE a = $0", i));
  }
  ASSERT_OK(conn.Execute("ALTER TABLE t1 ADD COLUMN d text"));
  for (int i = 1; i <= 40; ++i) {
    ASSERT_OK(conn.ExecuteFormat("UPDATE t1 SET d = 'd$0' WHERE a = $0", i));
  }
  ASSERT_OK(conn.Execute("ALTER TABLE t1 DROP COLUMN c"));
  for (int i = 1; i <= 40; ++i) {
    ASSERT_OK(conn.ExecuteFormat("UPDATE t1 SET b = 'w$0' WHERE a = $0", i));
  }
  ASSERT_OK(conn.Execute("COMMIT"));

  auto find_col = [](const RowMessage& row, const std::string& name) -> const DatumMessagePB* {
    for (const auto& col : row.new_tuple()) {
      if (col.column_name() == name) {
        return &col;
      }
    }
    return nullptr;
  };
  auto col_has_value = [](const DatumMessagePB* col) {
    return col != nullptr && col->datum_case() != DatumMessagePB::DATUM_NOT_SET &&
           !col->has_datum_missing();
  };

  std::map<int32_t, RowMessage> inserts;
  std::map<int32_t, std::vector<RowMessage>> updates;
  auto collect = [&](const GetChangesResponsePB& resp) {
    for (const auto& record : resp.cdc_sdk_proto_records()) {
      const auto& row = record.row_message();
      if (row.table() != "t1") {
        continue;
      }
      const auto* a = find_col(row, "a");
      if (!col_has_value(a)) {
        continue;
      }
      if (row.op() == RowMessage::INSERT) {
        inserts[a->datum_int32()] = row;
      } else if (row.op() == RowMessage::UPDATE) {
        updates[a->datum_int32()].push_back(row);
      }
    }
  };

  auto change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
  ASSERT_FALSE(change_resp.cdc_sdk_checkpoint().key().empty());
  ASSERT_NE(change_resp.cdc_sdk_checkpoint().write_id(), 0);
  collect(change_resp);
  // The intent batch is smaller than the txn, so the restart happens before every update has
  // been streamed.
  size_t updates_before_restart = 0;
  for (const auto& entry : updates) {
    updates_before_restart += entry.second.size();
  }
  ASSERT_LT(updates_before_restart, 120);

  auto checkpoint = change_resp.cdc_sdk_checkpoint();
  auto* tserver = test_cluster()->mini_tablet_server(0);
  ASSERT_OK(tserver->Restart());
  ASSERT_OK(tserver->WaitStarted());

  bool streamed_after_restart = false;
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto resp = GetChangesFromCDCWithoutRetry(stream_id, tablets, &checkpoint);
        if (!resp.ok()) {
          if (!streamed_after_restart &&
              (resp.status().IsLeaderNotReadyToServe() || resp.status().IsNotFound())) {
            return false;
          }
          return resp.status();
        }
        streamed_after_restart = true;
        change_resp = *resp;
        collect(change_resp);
        checkpoint = change_resp.cdc_sdk_checkpoint();
        LOG(INFO) << "Post-restart GetChanges records=" << change_resp.cdc_sdk_proto_records_size()
                  << " key_empty=" << checkpoint.key().empty()
                  << " write_id=" << checkpoint.write_id();
        return checkpoint.key().empty() && checkpoint.write_id() == 0;
      },
      MonoDelta::FromSeconds(60) * kTimeMultiplier,
      "Waiting to finish streaming the transactional DDL txn after tserver restart"));

  ASSERT_EQ(inserts.size(), 40);
  ASSERT_EQ(updates.size(), 40);
  std::string txn_id;
  for (int i = 1; i <= 40; ++i) {
    auto insert_it = inserts.find(i);
    ASSERT_NE(insert_it, inserts.end()) << i;
    const auto& inserted = insert_it->second;
    ASSERT_EQ(find_col(inserted, "b")->datum_string(), Format("v$0", i));
    ASSERT_TRUE(col_has_value(find_col(inserted, "c"))) << i;
    ASSERT_EQ(find_col(inserted, "c")->datum_int32(), i);
    ASSERT_FALSE(col_has_value(find_col(inserted, "d"))) << i;

    auto update_it = updates.find(i);
    ASSERT_NE(update_it, updates.end()) << i;
    const auto& row_updates = update_it->second;
    ASSERT_EQ(row_updates.size(), 3) << i;

    const auto& set_b = row_updates[0];
    const auto& set_d = row_updates[1];
    const auto& set_b_after_drop = row_updates[2];
    ASSERT_EQ(find_col(set_b, "b")->datum_string(), Format("u$0", i));
    ASSERT_TRUE(col_has_value(find_col(set_b, "c"))) << i;
    ASSERT_EQ(find_col(set_b, "c")->datum_int32(), i);
    ASSERT_FALSE(col_has_value(find_col(set_b, "d"))) << i;

    ASSERT_EQ(find_col(set_d, "d")->datum_string(), Format("d$0", i));
    ASSERT_TRUE(col_has_value(find_col(set_d, "c"))) << i;

    ASSERT_EQ(find_col(set_b_after_drop, "b")->datum_string(), Format("w$0", i));
    ASSERT_EQ(find_col(set_b_after_drop, "d")->datum_string(), Format("d$0", i));
    // c existed before the transaction, so the packed row written after DROP COLUMN still carries
    // it until commit removes the column.
    if (col_has_value(find_col(set_b_after_drop, "c"))) {
      ASSERT_EQ(find_col(set_b_after_drop, "c")->datum_int32(), i);
    }

    for (const auto& row : row_updates) {
      ASSERT_FALSE(row.transaction_id().empty()) << i;
      if (txn_id.empty()) {
        txn_id = row.transaction_id();
      } else {
        ASSERT_EQ(row.transaction_id(), txn_id) << i;
      }
    }
  }
}

}  // namespace cdc
}  // namespace yb
