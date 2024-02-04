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

#include "yb/tserver/xcluster_consumer_replication_error.h"
#include "yb/util/test_util.h"

namespace yb::tserver {

using XClusterReplicationErrorSendState::kNotSent;
using XClusterReplicationErrorSendState::kSending;
using XClusterReplicationErrorSendState::kSent;

void ValidateErrorInMap(
    const XClusterReplicationErrorsToSendMap& errors_map, const XClusterPollerId& poller_id,
    ReplicationErrorPb error) {
  ASSERT_TRUE(
      errors_map.contains(poller_id.replication_group_id) &&
      errors_map.at(poller_id.replication_group_id).contains(poller_id.consumer_table_id) &&
      errors_map.at(poller_id.replication_group_id)
          .at(poller_id.consumer_table_id)
          .contains(poller_id.producer_tablet_id));

  auto& poller_error = errors_map.at(poller_id.replication_group_id)
                           .at(poller_id.consumer_table_id)
                           .at(poller_id.producer_tablet_id);

  ASSERT_EQ(poller_error.consumer_term, poller_id.leader_term);
  ASSERT_EQ(poller_error.error, error);
}

TEST(XClusterConsumerReplicationError, TestCollector) {
  XClusterConsumerReplicationErrorCollector collector;

  // Setup 3 pollers across 2 Replication Groups, 2 tables and 3 tablets.
  const XClusterPollerId poller1(
      cdc::ReplicationGroupId("Group1"), "TableA", "Tablet1", /*leader_term=*/1);
  const XClusterPollerId poller2(
      cdc::ReplicationGroupId("Group1"), "TableA", "Tablet2", /*leader_term=*/2);
  const XClusterPollerId poller3(
      cdc::ReplicationGroupId("Group2"), "TableA", "Tablet1", /*leader_term=*/1);
  collector.AddPoller(poller1);
  collector.AddPoller(poller2);
  collector.AddPoller(poller3);

  // Validate the initial state.
  ASSERT_EQ(collector.TEST_GetSendState(), kSent);
  auto error_map = collector.TEST_GetErrorMap();
  ASSERT_EQ(error_map.size(), 3);
  ASSERT_EQ(error_map[poller1].send_state, kSent);
  ASSERT_EQ(error_map[poller2].send_state, kSent);
  ASSERT_EQ(error_map[poller3].send_state, kSent);
  auto result = collector.GetErrorsToSend(/* get_all_errors */ true);
  ASSERT_EQ(result.size(), 0);
  ASSERT_EQ(collector.TEST_GetSendState(), kSent);

  // Store error for Poller1.
  collector.StoreError(poller1, ReplicationErrorPb::REPLICATION_MISSING_OP_ID);
  ASSERT_EQ(collector.TEST_GetSendState(), kNotSent);
  error_map = collector.TEST_GetErrorMap();
  ASSERT_EQ(error_map[poller1].send_state, kNotSent);
  ASSERT_EQ(error_map[poller2].send_state, kSent);
  ASSERT_EQ(error_map[poller3].send_state, kSent);

  // Verify that we only get the one error.
  result = collector.GetErrorsToSend(/* get_all_errors */ false);
  ASSERT_EQ(result.size(), 1);
  ASSERT_NO_FATALS(
      ValidateErrorInMap(result, poller1, ReplicationErrorPb::REPLICATION_MISSING_OP_ID));
  ASSERT_EQ(collector.TEST_GetSendState(), kSending);
  error_map = collector.TEST_GetErrorMap();
  ASSERT_EQ(error_map[poller1].send_state, kSending);
  ASSERT_EQ(error_map[poller2].send_state, kSent);
  ASSERT_EQ(error_map[poller3].send_state, kSent);

  // Store error for Poller3.
  collector.StoreError(poller3, ReplicationErrorPb::REPLICATION_SCHEMA_MISMATCH);
  ASSERT_EQ(collector.TEST_GetSendState(), kNotSent);
  error_map = collector.TEST_GetErrorMap();
  ASSERT_EQ(error_map[poller1].send_state, kSending);
  ASSERT_EQ(error_map[poller2].send_state, kSent);
  ASSERT_EQ(error_map[poller3].send_state, kNotSent);

  // Verify that we get two errors.
  result = collector.GetErrorsToSend(/* get_all_errors */ false);
  ASSERT_EQ(result.size(), 2);
  ASSERT_NO_FATALS(
      ValidateErrorInMap(result, poller1, ReplicationErrorPb::REPLICATION_MISSING_OP_ID));
  ASSERT_NO_FATALS(
      ValidateErrorInMap(result, poller3, ReplicationErrorPb::REPLICATION_SCHEMA_MISMATCH));
  ASSERT_EQ(collector.TEST_GetSendState(), kSending);
  error_map = collector.TEST_GetErrorMap();
  ASSERT_EQ(error_map[poller1].send_state, kSending);
  ASSERT_EQ(error_map[poller2].send_state, kSent);
  ASSERT_EQ(error_map[poller3].send_state, kSending);

  // Validate TransitionErrorsFromSendingToSent.
  collector.TransitionErrorsFromSendingToSent();
  ASSERT_EQ(collector.TEST_GetSendState(), kSent);
  error_map = collector.TEST_GetErrorMap();
  ASSERT_EQ(error_map[poller1].send_state, kSent);
  ASSERT_EQ(error_map[poller2].send_state, kSent);
  ASSERT_EQ(error_map[poller3].send_state, kSent);

  // Store error for Poller2.
  collector.StoreError(poller2, ReplicationErrorPb::REPLICATION_OK);
  ASSERT_EQ(collector.TEST_GetSendState(), kNotSent);
  error_map = collector.TEST_GetErrorMap();
  ASSERT_EQ(error_map[poller1].send_state, kSent);
  ASSERT_EQ(error_map[poller2].send_state, kNotSent);
  ASSERT_EQ(error_map[poller3].send_state, kSent);

  // Verify that we get only one error.
  result = collector.GetErrorsToSend(/* get_all_errors */ false);
  ASSERT_EQ(result.size(), 1);
  ASSERT_NO_FATALS(ValidateErrorInMap(result, poller2, ReplicationErrorPb::REPLICATION_OK));
  ASSERT_EQ(collector.TEST_GetSendState(), kSending);
  error_map = collector.TEST_GetErrorMap();
  ASSERT_EQ(error_map[poller1].send_state, kSent);
  ASSERT_EQ(error_map[poller2].send_state, kSending);
  ASSERT_EQ(error_map[poller3].send_state, kSent);

  // Validate TransitionErrorsFromSendingToSent.
  collector.TransitionErrorsFromSendingToSent();
  ASSERT_EQ(collector.TEST_GetSendState(), kSent);
  error_map = collector.TEST_GetErrorMap();
  ASSERT_EQ(error_map[poller1].send_state, kSent);
  ASSERT_EQ(error_map[poller2].send_state, kSent);
  ASSERT_EQ(error_map[poller3].send_state, kSent);

  // Verify that we get no errors.
  result = collector.GetErrorsToSend(/* get_all_errors */ false);
  ASSERT_EQ(result.size(), 0);
  ASSERT_EQ(collector.TEST_GetSendState(), kSent);
  error_map = collector.TEST_GetErrorMap();
  ASSERT_EQ(error_map[poller1].send_state, kSent);
  ASSERT_EQ(error_map[poller2].send_state, kSent);
  ASSERT_EQ(error_map[poller3].send_state, kSent);

  // Get all errors after they have been marked as Sent.
  result = collector.GetErrorsToSend(/* get_all_errors */ true);
  ASSERT_EQ(result.size(), 2);
  ASSERT_NO_FATALS(
      ValidateErrorInMap(result, poller1, ReplicationErrorPb::REPLICATION_MISSING_OP_ID));
  ASSERT_NO_FATALS(ValidateErrorInMap(result, poller2, ReplicationErrorPb::REPLICATION_OK));
  ASSERT_NO_FATALS(
      ValidateErrorInMap(result, poller3, ReplicationErrorPb::REPLICATION_SCHEMA_MISMATCH));
  ASSERT_EQ(collector.TEST_GetSendState(), kSending);
  error_map = collector.TEST_GetErrorMap();
  ASSERT_EQ(error_map[poller1].send_state, kSending);
  ASSERT_EQ(error_map[poller2].send_state, kSending);
  ASSERT_EQ(error_map[poller3].send_state, kSending);
}

}  // namespace yb::tserver
