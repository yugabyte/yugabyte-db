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

#include <gtest/gtest.h>

#include "yb/common/pgsql_error.h"
#include "yb/common/transaction.pb.h"

#include "yb/consensus/consensus.messages.h"

#include "yb/rpc/lightweight_message.h"

#include "yb/tablet/persistence_gate.h"

#include "yb/tserver/tserver_error.h"

#include "yb/util/metrics.h"
#include "yb/util/test_util.h"

DECLARE_bool(enable_persistence_gate);
DECLARE_bool(enforce_xcluster_guarded_lease);

METRIC_DECLARE_entity(server);
METRIC_DECLARE_counter(persistence_violation_rejections);

namespace yb::tablet {

namespace {

constexpr uint32_t kDatabaseOid = 16384;
constexpr uint32_t kOtherDatabaseOid = 16385;
constexpr uint32_t kStoppedDatabaseOid = 16386;

constexpr int64_t kTerm = 3;

const HybridTime kBlockedThroughHt(HybridTime::FromMicros(1'000'000));

}  // namespace

class PersistenceGateTest : public YBTest {
 protected:
  void SetUp() override {
    YBTest::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_persistence_gate) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enforce_xcluster_guarded_lease) = true;
    metric_entity_ = METRIC_ENTITY_server.Instantiate(&metric_registry_, "server");
    gate_ = std::make_unique<PersistenceGate>(
        [this](int64_t op_term, uint32_t database_oid) -> Result<std::optional<HybridTime>> {
          ++lookups_;
          last_lookup_term_ = op_term;
          RETURN_NOT_OK(lookup_status_);
          switch (database_oid) {
            case kDatabaseOid:
              return kBlockedThroughHt;
            case kStoppedDatabaseOid:
              return HybridTime::kMax;
            default:
              return std::nullopt;
          }
        },
        metric_entity_);
  }

  int64_t GetRefusals() {
    return METRIC_persistence_violation_rejections.Instantiate(metric_entity_)->value();
  }

  static std::shared_ptr<consensus::LWReplicateMsg> MakeStampedWrite(
      uint32_t database_oid, HybridTime origination_ht) {
    auto msg = rpc::MakeSharedMessage<consensus::LWReplicateMsg>();
    msg->set_op_type(consensus::OperationType::WRITE_OP);
    msg->mutable_id()->set_term(kTerm);
    auto& origination_info = *msg->mutable_write()->mutable_origination_info();
    origination_info.set_database_oid(database_oid);
    origination_info.set_origination_ht(origination_ht.ToUint64());
    return msg;
  }

  static std::shared_ptr<consensus::LWReplicateMsg> MakeStampedTransactionUpdate(
      TransactionStatus status, uint32_t database_oid, HybridTime origination_ht) {
    auto msg = rpc::MakeSharedMessage<consensus::LWReplicateMsg>();
    msg->set_op_type(consensus::OperationType::UPDATE_TRANSACTION_OP);
    msg->mutable_id()->set_term(kTerm);
    auto& state = *msg->mutable_transaction_state();
    state.set_status(status);
    state.mutable_origination_info()->set_database_oid(database_oid);
    state.mutable_origination_info()->set_origination_ht(origination_ht.ToUint64());
    return msg;
  }

  Status CheckPersistAllowed(const std::shared_ptr<consensus::LWReplicateMsg>& msg) {
    return gate_->CheckPersistAllowed(*msg);
  }

  // Expects status to be a refusal of a blocked op: final, and reported to Postgres as a read-only
  // database.
  static void ExpectBlocked(const Status& status, uint32_t database_oid = kDatabaseOid) {
    ASSERT_TRUE(status.IsExpired()) << status;
    ASSERT_EQ(
        tserver::TabletServerError(status).value(),
        tserver::TabletServerErrorPB::PERSISTING_BLOCKED)
        << status;
    ASSERT_EQ(PgsqlError(status).value(), YBPgErrorCode::YB_PG_READ_ONLY_SQL_TRANSACTION) << status;
    ASSERT_STR_CONTAINS(
        status.message().ToBuffer(),
        Format("database with OID $0 are temporarily disabled", database_oid));
  }

  MetricRegistry metric_registry_;
  scoped_refptr<MetricEntity> metric_entity_;

  std::unique_ptr<PersistenceGate> gate_;
  // These are updated by calls to the lookup function we pass to gate_.
  int lookups_ = 0;
  int64_t last_lookup_term_ = -1;
  // If not okay, this is the Status the lookup function will return the next time it's called.
  Status lookup_status_;
};

// Checks that a write that originated exactly at its database's entry is refused, with a final
// status, and counted.
TEST_F(PersistenceGateTest, RefusesWriteAtEntry) {
  ASSERT_NO_FATALS(
      ExpectBlocked(CheckPersistAllowed(MakeStampedWrite(kDatabaseOid, kBlockedThroughHt))));
  ASSERT_EQ(GetRefusals(), 1);
}

// Checks that a write that originated before its database's entry is refused and counted.
TEST_F(PersistenceGateTest, RefusesWriteBeforeEntry) {
  ASSERT_NO_FATALS(ExpectBlocked(
      CheckPersistAllowed(MakeStampedWrite(kDatabaseOid, kBlockedThroughHt.Decremented()))));
  ASSERT_EQ(GetRefusals(), 1);
}

// Checks that a write that originated after its database's entry passes and is not counted.
TEST_F(PersistenceGateTest, AllowsWriteAfterEntry) {
  ASSERT_OK(CheckPersistAllowed(MakeStampedWrite(kDatabaseOid, kBlockedThroughHt.Incremented())));
  ASSERT_EQ(GetRefusals(), 0);
}

// Checks that a kMax entry blocks every write, however late it originated.
TEST_F(PersistenceGateTest, MaxEntryRefusesEveryWrite) {
  const auto far_future_time = HybridTime::FromMicros(GetCurrentTimeMicros()).AddSeconds(1'000'000);
  ASSERT_NO_FATALS(ExpectBlocked(
      CheckPersistAllowed(MakeStampedWrite(kStoppedDatabaseOid, far_future_time)),
      kStoppedDatabaseOid));
  ASSERT_EQ(GetRefusals(), 1);
}

// Checks that a write to a database without an entry passes and is not counted.
TEST_F(PersistenceGateTest, AllowsWriteWithoutEntry) {
  ASSERT_OK(CheckPersistAllowed(MakeStampedWrite(kOtherDatabaseOid, kBlockedThroughHt)));
  ASSERT_EQ(GetRefusals(), 0);
}

// Checks that the lookup is given the term of the op being judged.
TEST_F(PersistenceGateTest, LookupGetsOpTerm) {
  ASSERT_OK(CheckPersistAllowed(MakeStampedWrite(kOtherDatabaseOid, kBlockedThroughHt)));
  ASSERT_EQ(last_lookup_term_, kTerm);
}

// Checks that an unstamped write (e.g., YCQL or an xCluster apply) passes without a lookup.
TEST_F(PersistenceGateTest, AllowsUnstampedWrite) {
  auto unstamped_write = rpc::MakeSharedMessage<consensus::LWReplicateMsg>();
  unstamped_write->set_op_type(consensus::OperationType::WRITE_OP);
  unstamped_write->mutable_write();
  ASSERT_OK(CheckPersistAllowed(unstamped_write));
  ASSERT_EQ(lookups_, 0);
}

// Checks that stamped COMMITTED and SEALED updates are judged (a sealed transaction commits once
// its writes land), while other transaction status updates pass without a lookup.
TEST_F(PersistenceGateTest, JudgesOnlyCommits) {
  ASSERT_OK(CheckPersistAllowed(
      MakeStampedTransactionUpdate(TransactionStatus::ABORTED, kDatabaseOid, kBlockedThroughHt)));
  ASSERT_OK(CheckPersistAllowed(
      MakeStampedTransactionUpdate(TransactionStatus::APPLYING, kDatabaseOid, kBlockedThroughHt)));
  // Not even looked up: a failed lookup (no lease) would otherwise refuse them.
  ASSERT_EQ(lookups_, 0);

  ASSERT_NO_FATALS(ExpectBlocked(CheckPersistAllowed(MakeStampedTransactionUpdate(
      TransactionStatus::COMMITTED, kDatabaseOid, kBlockedThroughHt))));
  ASSERT_NO_FATALS(ExpectBlocked(CheckPersistAllowed(
      MakeStampedTransactionUpdate(TransactionStatus::SEALED, kDatabaseOid, kBlockedThroughHt))));
}

// Checks that a ServiceUnavailable lookup error (cannot tell) refuses the op retryably with the
// persisting-blocked error codes added; that any other lookup error refuses it unchanged; and that
// neither is counted.
TEST_F(PersistenceGateTest, LookupErrorRefusesUncounted) {
  lookup_status_ = STATUS(ServiceUnavailable, "No lease");
  auto status = CheckPersistAllowed(MakeStampedWrite(kOtherDatabaseOid, kBlockedThroughHt));
  ASSERT_TRUE(status.IsServiceUnavailable()) << status;
  ASSERT_EQ(
      tserver::TabletServerError(status).value(), tserver::TabletServerErrorPB::PERSISTING_BLOCKED)
      << status;
  ASSERT_EQ(PgsqlError(status).value(), YBPgErrorCode::YB_PG_READ_ONLY_SQL_TRANSACTION) << status;
  ASSERT_STR_CONTAINS(status.message().ToBuffer(), "No lease");

  lookup_status_ = STATUS(LeaderNotReadyToServe, "Catalog not loaded");
  status = CheckPersistAllowed(MakeStampedWrite(kOtherDatabaseOid, kBlockedThroughHt));
  ASSERT_TRUE(status.IsLeaderNotReadyToServe()) << status;
  ASSERT_FALSE(tserver::TabletServerError::ValueFromStatus(status)) << status;
  ASSERT_FALSE(PgsqlError::ValueFromStatus(status)) << status;

  ASSERT_EQ(GetRefusals(), 0);
}

// Checks that with either flag off nothing is refused and the lookup is not called.
TEST_F(PersistenceGateTest, NotEnforcedWithEitherFlagOff) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_persistence_gate) = false;
  ASSERT_OK(CheckPersistAllowed(MakeStampedWrite(kDatabaseOid, kBlockedThroughHt)));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_persistence_gate) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enforce_xcluster_guarded_lease) = false;
  ASSERT_OK(CheckPersistAllowed(MakeStampedWrite(kDatabaseOid, kBlockedThroughHt)));
  ASSERT_EQ(lookups_, 0);
}

}  // namespace yb::tablet
