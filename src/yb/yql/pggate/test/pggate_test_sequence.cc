//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include <limits>

#include "yb/util/yb_pg_errcodes.h"
#include "yb/yql/pggate/test/pggate_test.h"
#include "yb/yql/pggate/ybc_pggate.h"

namespace yb {
namespace pggate {

constexpr auto kMaxInt64 = std::numeric_limits<int64_t>::max();
constexpr auto kMinInt64 = std::numeric_limits<int64_t>::min();

class PggateTestSequence : public PggateTest {
  static const YBCPgOid db_oid_ = kDefaultDatabaseOid;
  static const YBCPgOid seq_oid_ = 3;
  static const uint64_t catalog_version_ = 1;

 protected:
  // Create sequence with specified initial value and "called" status
  static YBCStatus CreateSequence(int64_t init_val, bool is_called) {
    return YBCInsertSequenceTuple(db_oid_, seq_oid_, catalog_version_,
                                  false /*is_db_catalog_version_mode*/,
                                  init_val, is_called);
  }

  // Drop sequence
  static YBCStatus DropSequence() {
    YBCPgStatement pg_stmt(nullptr);
    CHECK_YBC_STATUS(YBCPgNewDropSequence(db_oid_, seq_oid_, &pg_stmt));
    return YBCPgExecDropSequence(pg_stmt);
  }

  // Read the value and "called" status from sequence
  static YBCStatus ReadSequence(int64_t *last_val, bool *is_called) {
    return YBCReadSequenceTuple(db_oid_, seq_oid_, catalog_version_,
                                false /*is_db_catalog_version_mode*/,
                                last_val, is_called);
  }

  // Unconditionally update the value and "called" status of sequence
  static YBCStatus UpdateSequence(int64_t last_val, bool is_called) {
    bool dummy;
    return YBCUpdateSequenceTuple(db_oid_, seq_oid_, catalog_version_,
                                  false /*is_db_catalog_version_mode*/,
                                  last_val, is_called, &dummy);
  }

  // Update the value and "called" status of sequence if current state of the sequence matches
  // expectation. Return "true" if update skipped due to state mismatch
  static bool UpdateSequenceConditionally(int64_t new_last_val, bool new_is_called,
                                   int64_t expected_last_val, bool expected_is_called) {
    bool skipped;
    CHECK_YBC_STATUS(YBCUpdateSequenceTupleConditionally(db_oid_, seq_oid_, catalog_version_,
                                                         false /*is_db_catalog_version_mode*/,
                                                         new_last_val, new_is_called,
                                                         expected_last_val, expected_is_called,
                                                         &skipped));
    return skipped;
  }

  // Fetch up to specified non-zero number of values from the sequence using specified parameters
  // (increment, limits, whether allow to wrap around) updating the state accordingly
  // Returns the range, as inclusive boundaries.
  static YBCStatus FetchSequence(uint32_t fetch_count, int64_t increment,
                          int64_t min_value, int64_t max_value, bool cycle,
                          int64_t *first_value, int64_t *last_value) {
    return YBCFetchSequenceTuple(db_oid_, seq_oid_, catalog_version_,
                                 false /*is_db_catalog_version_mode*/,
                                 fetch_count, increment, min_value, max_value, cycle,
                                 first_value, last_value);
  }

  // Update the sequence status, fetch it right away with specified parameters and check
  // if the fetch result matches expected
  static void SetAndFetchSequence(int64_t last_val, bool is_called,
                           uint32_t fetch_count, int64_t increment,
                           int64_t min_value, int64_t max_value,
                           int64_t expected_first_value, int64_t expected_last_value) {
    int64_t fetched_first_value;
    int64_t fetched_last_value;
    CHECK_YBC_STATUS(UpdateSequence(last_val, is_called));
    // Always cycle, we do not want to handle errors due to limit reached
    CHECK_YBC_STATUS(FetchSequence(fetch_count, increment, min_value, max_value, true,
                                   &fetched_first_value, &fetched_last_value));
    CHECK_EQ(fetched_first_value, expected_first_value);
    CHECK_EQ(fetched_last_value, expected_last_value);
  }
};

TEST_F(PggateTestSequence, TestSequenceUpdate) {
  CHECK_OK(Init("TestSequenceUpdate"));

  int64_t last_val = 1;
  bool is_called = false;

  // Create sequence in the connected database.
  CHECK_YBC_STATUS(CreateSequence(last_val, is_called));

  int64_t read_last_val;
  bool read_is_called;
  CHECK_YBC_STATUS(ReadSequence(&read_last_val, &read_is_called));
  CHECK_EQ(last_val, read_last_val);
  CHECK_EQ(is_called, read_is_called);

  last_val = 100;
  is_called = true;
  bool skipped = UpdateSequenceConditionally(last_val, is_called, /* new state */
                                             read_last_val, read_is_called /* expected state */);
  CHECK_EQ(skipped, false);

  CHECK_YBC_STATUS(ReadSequence(&read_last_val, &read_is_called));
  CHECK_EQ(last_val, read_last_val);
  CHECK_EQ(is_called, read_is_called);

  last_val = 333;
  is_called = true;
  CHECK_YBC_STATUS(UpdateSequence(last_val, is_called));

  last_val = 200;
  is_called = true;
  skipped = UpdateSequenceConditionally(last_val, is_called, /* new state */
                                        read_last_val, read_is_called /* expected state */);
  CHECK_EQ(skipped, true);

  CHECK_YBC_STATUS(ReadSequence(&read_last_val, &read_is_called));
  last_val = 433;
  is_called = true;
  skipped = UpdateSequenceConditionally(last_val, is_called, /* new state */
                                        read_last_val, read_is_called /* expected state */);
  CHECK_EQ(skipped, false);

  CHECK_YBC_STATUS(DropSequence());
}

TEST_F(PggateTestSequence, TestSequenceFetchAscending) {
  CHECK_OK(Init("TestSequenceFetchAscending"));

  // Create sequence in the connected database.
  CHECK_YBC_STATUS(CreateSequence(1, false));
  // arg order: (last_val, is_called, fetch_count, increment, min_value, max_value,
  //             expected_first_value, expected_last_value)
  SetAndFetchSequence(10000, true, 100, 1, 1, 10000,
                      1, 100); // expected range
  SetAndFetchSequence(9999, true, 100, 1, 1, 10000,
                      10000, 10000); // expected range
  SetAndFetchSequence(1, true, 100, 10, 1, 10000,
                      11, 1001); // expected range
  SetAndFetchSequence(10000, true, 100, 10, 1, 10000,
                      1, 991); // expected range
  SetAndFetchSequence(9999, true, 100, 10, 1, 10000,
                      1, 991); // expected range
  SetAndFetchSequence(1, true, 100, 1, 1, 2,
                      2, 2); // expected range
  SetAndFetchSequence(2, true, 100, 1, 1, 2,
                      1, 2); // expected range
  SetAndFetchSequence(kMinInt64, true, 100, 1, kMinInt64, kMaxInt64,
                      kMinInt64 + 1, kMinInt64 + 100); // expected range
  SetAndFetchSequence(kMaxInt64, true, 100, 1, kMinInt64, kMaxInt64,
                      kMinInt64, kMinInt64 + 99); // expected range
  SetAndFetchSequence(1, true, 100, 922337203685477580, 1, kMaxInt64,
                      922337203685477581, 9223372036854775801); // expected range
  SetAndFetchSequence(kMaxInt64, true, 100, 922337203685477580, 1, kMaxInt64,
                      1, 9223372036854775801); // expected range
  SetAndFetchSequence(kMaxInt64 - 1, true, 100, 1, 1, kMaxInt64,
                      kMaxInt64, kMaxInt64); // expected range
  SetAndFetchSequence(kMaxInt64, true, 100, 1, 1, kMaxInt64,
                      1, 100); // expected range

  SetAndFetchSequence(10000, false, 100, 1, 1, 10000,
                      10000, 10000); // expected range
  SetAndFetchSequence(9999, false, 100, 1, 1, 10000,
                      9999, 10000); // expected range
  SetAndFetchSequence(1, false, 100, 10, 1, 10000,
                      1, 991); // expected range
  SetAndFetchSequence(10000, false, 100, 10, 1, 10000,
                      10000, 10000); // expected range
  SetAndFetchSequence(9999, false, 100, 10, 1, 10000,
                      9999, 9999); // expected range
  SetAndFetchSequence(1, false, 100, 1, 1, 2,
                      1, 2); // expected range
  SetAndFetchSequence(2, false, 100, 1, 1, 2,
                      2, 2); // expected range
  SetAndFetchSequence(kMinInt64, false, 100, 1, kMinInt64, kMaxInt64,
                      kMinInt64, kMinInt64 + 99); // expected range
  SetAndFetchSequence(kMaxInt64, false, 100, 1, kMinInt64, kMaxInt64,
                      kMaxInt64, kMaxInt64); // expected range
  SetAndFetchSequence(1, false, 100, 922337203685477580, 1, kMaxInt64,
                      1, 9223372036854775801); // expected range
  SetAndFetchSequence(kMaxInt64, false, 100, 922337203685477580, 1, kMaxInt64,
                      kMaxInt64, kMaxInt64); // expected range
  SetAndFetchSequence(kMaxInt64 - 1, false, 100, 1, 1, kMaxInt64,
                      kMaxInt64 - 1, kMaxInt64); // expected range
  SetAndFetchSequence(kMaxInt64, false, 100, 1, 1, kMaxInt64,
                      kMaxInt64, kMaxInt64); // expected range
}

TEST_F(PggateTestSequence, TestSequenceFetchDescending) {
  CHECK_OK(Init("TestSequenceFetchDescending"));

  // Create sequence in the connected database.
  CHECK_YBC_STATUS(CreateSequence(1, false));
  // arg order: (last_val, is_called, fetch_count, increment, min_value, max_value,
  //             expected_first_value, expected_last_value)
  SetAndFetchSequence(1, true, 100, -1, 1, 10000,
                      10000, 9901); // expected range
  SetAndFetchSequence(2, true, 100, -1, 1, 10000,
                      1, 1); // expected range
  SetAndFetchSequence(10000, true, 100, -10, 1, 10000,
                      9990, 9000); // expected range
  SetAndFetchSequence(1, true, 100, -10, 1, 10000,
                      10000, 9010); // expected range
  SetAndFetchSequence(2, true, 100, -10, 1, 10000,
                      10000, 9010); // expected range
  SetAndFetchSequence(1, true, 100, -1, 1, 2,
                      2, 1); // expected range
  SetAndFetchSequence(2, true, 100, -1, 1, 2,
                      1, 1); // expected range
  SetAndFetchSequence(kMinInt64, true, 100, -1, kMinInt64, kMaxInt64,
                      kMaxInt64, kMaxInt64 - 99); // expected range
  SetAndFetchSequence(kMaxInt64, true, 100, -1, kMinInt64, kMaxInt64,
                      kMaxInt64 - 1, kMaxInt64 - 100); // expected range
  SetAndFetchSequence(1, true, 100, -922337203685477580, 1, kMaxInt64,
                      kMaxInt64, 7); // expected range
  SetAndFetchSequence(kMaxInt64, true, 100, -922337203685477580, 1, kMaxInt64,
                      8301034833169298227, 7); // expected range
  SetAndFetchSequence(2, true, 100, -1, 1, kMaxInt64,
                      1, 1); // expected range
  SetAndFetchSequence(1, true, 100, -1, 1, kMaxInt64,
                      kMaxInt64, kMaxInt64 - 99); // expected range

  SetAndFetchSequence(1, false, 100, -1, 1, 10000,
                      1, 1); // expected range
  SetAndFetchSequence(2, false, 100, -1, 1, 10000,
                      2, 1); // expected range
  SetAndFetchSequence(10000, false, 100, -10, 1, 10000,
                      10000, 9010); // expected range
  SetAndFetchSequence(1, false, 100, -10, 1, 10000,
                      1, 1); // expected range
  SetAndFetchSequence(2, false, 100, -10, 1, 10000,
                      2, 2); // expected range
  SetAndFetchSequence(1, false, 100, -1, 1, 2,
                      1, 1); // expected range
  SetAndFetchSequence(2, false, 100, -1, 1, 2,
                      2, 1); // expected range
  SetAndFetchSequence(kMinInt64, false, 100, -1, kMinInt64, kMaxInt64,
                      kMinInt64, kMinInt64); // expected range
  SetAndFetchSequence(kMaxInt64, false, 100, -1, kMinInt64, kMaxInt64,
                      kMaxInt64, kMaxInt64 - 99); // expected range
  SetAndFetchSequence(1, false, 100, -922337203685477580, 1, kMaxInt64,
                      1, 1); // expected range
  SetAndFetchSequence(kMaxInt64, false, 100, -922337203685477580, 1, kMaxInt64,
                      kMaxInt64, 7); // expected range
  SetAndFetchSequence(2, false, 100, -1, 1, kMaxInt64,
                      2, 1); // expected range
  SetAndFetchSequence(1, false, 100, -1, 1, kMaxInt64,
                      1, 1); // expected range
}

TEST_F(PggateTestSequence, TestSequenceFetchMisc) {
  CHECK_OK(Init("TestSequenceFetchMisc"));

  int64_t last_val = 1;
  bool is_called = false;

  // Create sequence in the connected database.
  CHECK_YBC_STATUS(CreateSequence(last_val, is_called));
  int64_t read_last_val;
  bool read_is_called;
  CHECK_YBC_STATUS(ReadSequence(&read_last_val, &read_is_called));
  CHECK_EQ(last_val, read_last_val);
  CHECK_EQ(is_called, read_is_called);

  int64_t fetch_first_val;
  int64_t fetch_last_val;
  CHECK_YBC_STATUS(FetchSequence(100 /* fetch_count */, 1 /* inc_by */,
                                 kMinInt64, kMaxInt64, false /* limits, cycle */,
                                 &fetch_first_val, &fetch_last_val));
  CHECK_EQ(fetch_first_val, 1);
  CHECK_EQ(fetch_last_val, 100);

  CHECK_YBC_STATUS(FetchSequence(100 /* fetch_count */, 1 /* inc_by */,
                                 1, 150, false /* limits, cycle */,
                                 &fetch_first_val, &fetch_last_val));
  CHECK_EQ(fetch_first_val, 101);
  CHECK_EQ(fetch_last_val, 150);

  YBCStatus fail = FetchSequence(100 /* fetch_count */, 1 /* inc_by */,
                                 1, 150, false /* limits, cycle */,
                                 &fetch_first_val, &fetch_last_val);
  EXPECT_EQ(YBCStatusPgsqlError(fail),
            to_underlying(YBPgErrorCode::YB_PG_SEQUENCE_GENERATOR_LIMIT_EXCEEDED));
  YBCFreeStatus(fail);

  CHECK_YBC_STATUS(FetchSequence(100 /* fetch_count */, 1 /* inc_by */,
                                 1, 150, true /* limits, cycle */,
                                 &fetch_first_val, &fetch_last_val));
  CHECK_EQ(fetch_first_val, 1);
  CHECK_EQ(fetch_last_val, 100);

  CHECK_YBC_STATUS(FetchSequence(100 /* fetch_count */, -10 /* inc_by */,
                                 1, 150, true /* limits, cycle */,
                                 &fetch_first_val, &fetch_last_val));
  CHECK_EQ(fetch_first_val, 90);
  CHECK_EQ(fetch_last_val, 10);

  CHECK_YBC_STATUS(FetchSequence(100 /* fetch_count */, -10 /* inc_by */,
                                 1, 150, true /* limits, cycle */,
                                 &fetch_first_val, &fetch_last_val));
  CHECK_EQ(fetch_first_val, 150);
  CHECK_EQ(fetch_last_val, 10);

  fail = FetchSequence(100 /* fetch_count */, -10 /* inc_by */,
                       1, 150, false /* limits, cycle */,
                       &fetch_first_val, &fetch_last_val);
  EXPECT_EQ(YBCStatusPgsqlError(fail),
            to_underlying(YBPgErrorCode::YB_PG_SEQUENCE_GENERATOR_LIMIT_EXCEEDED));
  YBCFreeStatus(fail);

  CHECK_YBC_STATUS(FetchSequence(1 /* fetch_count */, 1 /* inc_by */,
                                 1, 150, true /* limits, cycle */,
                                 &fetch_first_val, &fetch_last_val));
  CHECK_EQ(fetch_first_val, 11);
  CHECK_EQ(fetch_last_val, 11);

  CHECK_YBC_STATUS(DropSequence());
}

} // namespace pggate
} // namespace yb
