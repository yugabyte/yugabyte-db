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

#include <gtest/gtest.h>

#include "yb/common/hybrid_time.h"
#include "yb/server/logical_clock.h"
#include "yb/util/metrics.h"
#include "yb/util/test_util.h"

namespace yb {

METRIC_DEFINE_entity(lag_metric_test_entity);

class LagMetricsTest : public YBTest {
 public:
  void SetUp() override {
    YBTest::SetUp();

    entity_ = METRIC_ENTITY_lag_metric_test_entity.Instantiate(&registry_, "my-lag-metric-test");
  }

 protected:
  template <class LagType>
  void DoLagTest(MillisLagPrototype* metric) {
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    scoped_refptr<server::Clock> clock(
        server::LogicalClock::CreateStartingAt(HybridTime::FromMicros(micros)));

    auto lag = metric->Instantiate(entity_, clock);
    ASSERT_EQ(metric->description(), lag->prototype()->description());

    SleepFor(MonoDelta::FromMilliseconds(500));

    micros = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    clock->Update(HybridTime::FromMicros(micros));

    // Internal timestamp is set to the time when the metric was created.
    // So this lag is measure of the time elapsed since the metric was
    // created and the check time.
    ASSERT_GE(lag->lag_ms(), 500);
    SleepFor(MonoDelta::FromMilliseconds(1000));

    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    lag->UpdateTimestampInMilliseconds(now_ms);

    micros = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    clock->Update(HybridTime::FromMicros(micros));

    // Verify that the update happened correctly. The lag time should
    // be close to 0, but giving it extra time to account for slow
    // tests.
    ASSERT_LT(lag->lag_ms(), 200);

    // Set the timestamp to some time in the future to verify that the
    // metric can correctly deal with this case.
    lag->UpdateTimestampInMilliseconds(now_ms * 2);
    ASSERT_EQ(0, lag->lag_ms());
  }

  MetricRegistry registry_;
  scoped_refptr<MetricEntity> entity_;
};

METRIC_DEFINE_lag(lag_metric_test_entity, lag_simple, "Test MillisLag",
                  "Test MillisLag Description");
TEST_F(LagMetricsTest, SimpleLagTest) {
ASSERT_NO_FATALS(DoLagTest<MillisLag>(&METRIC_lag_simple));
}

METRIC_DEFINE_lag(lag_metric_test_entity, atomic_lag_simple, "Test Atomic MillisLag",
                  "Test Atomic MillisLag Description");
TEST_F(LagMetricsTest, SimpleAtomicLagTest) {
ASSERT_NO_FATALS(DoLagTest<AtomicMillisLag>(&METRIC_atomic_lag_simple));
}

} // namespace yb
