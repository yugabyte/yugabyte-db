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

#include "yb/tserver/pg_client_session_util.h"

#include <memory>

#include "yb/server/logical_clock.h"

#include "yb/util/test_util.h"

namespace yb::tserver {

class ReadPointHistoryTest : public YBTest {
 protected:
  std::shared_ptr<ConsistentReadPoint> NewReadPoint(uint64_t read_ht) {
    auto result = std::make_shared<ConsistentReadPoint>(clock_);
    result->SetReadTime(
        ReadHybridTime::SingleTime(HybridTime(read_ht)), ConsistentReadPoint::HybridTimeMap());
    return result;
  }

  scoped_refptr<ClockBase> clock_{server::LogicalClock::CreateStartingAt(HybridTime(1000))};
  ReadPointHistory history_{PrefixLogger(1)};
};

// A batch flushed with a read point may record restart info into it after the session has switched
// to another read point (#35498), so the history must keep the same object rather than a copy.
TEST_F(ReadPointHistoryTest, SavedReadPointKeepsReceivingUpdates) {
  auto read_point = NewReadPoint(100);
  history_.Save(read_point, 1);

  read_point->RestartRequired("tablet", ReadHybridTime::SingleTime(HybridTime(200)));

  auto restored = history_.Find(1);
  ASSERT_EQ(restored, read_point);
  ASSERT_TRUE(restored->IsRestartRequired());
}

TEST_F(ReadPointHistoryTest, Find) {
  history_.Save(NewReadPoint(100), 1);
  history_.Save(NewReadPoint(200), 2);

  ASSERT_EQ(history_.Find(1)->GetReadTime().read, HybridTime(100));
  ASSERT_EQ(history_.Find(2)->GetReadTime().read, HybridTime(200));
  ASSERT_EQ(history_.Find(3), nullptr);
  ASSERT_EQ(history_.Find(kInvalidReadTimeSerialNo), nullptr);

  history_.Cleanup(2);
  ASSERT_EQ(history_.Find(1), nullptr);
  ASSERT_NE(history_.Find(2), nullptr);
}

TEST_F(ReadPointHistoryTest, CleanupDoesNotInvalidateHeldReadPoints) {
  auto read_point = NewReadPoint(100);
  history_.Save(read_point, 1);

  history_.Cleanup(2);
  ASSERT_EQ(history_.Find(1), nullptr);
  ASSERT_EQ(read_point->GetReadTime().read, HybridTime(100));
}

} // namespace yb::tserver
