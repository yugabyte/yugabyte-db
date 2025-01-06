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

#include "yb/util/aggregate_stats.h"

#include <limits>

namespace yb {

AggregateStats::AggregateStats():
    total_sum_(0),
    total_count_(0),
    current_sum_(0),
    current_count_(0),
    min_value_(std::numeric_limits<int64_t>::max()),
    max_value_(std::numeric_limits<int64_t>::min()) {}

AggregateStats::AggregateStats(const AggregateStats& other):
    total_sum_(other.total_sum_.Load()),
    total_count_(other.total_count_.Load()),
    current_sum_(other.current_sum_.Load()),
    current_count_(other.current_count_.Load()),
    min_value_(other.min_value_.Load()),
    max_value_(other.max_value_.Load()) {}

AggregateStats::~AggregateStats() {
  aggregator_.SumIncrementBy(-total_sum_.Load());
  aggregator_.CountIncrementBy(-total_count_.Load());
}

void AggregateStats::IncrementBy(int64_t value, uint64_t count) {
  current_sum_.IncrementBy(value * count);
  current_count_.IncrementBy(count);
  min_value_.StoreMin(value);
  max_value_.StoreMax(value);
}

int64_t AggregateStats::MinValue() const {
  auto min = min_value_.Load();
  auto max = max_value_.Load();
  if (PREDICT_FALSE(min > max)) {
    return 0;
  }
  return min;
}

int64_t AggregateStats::MaxValue() const {
  auto min = min_value_.Load();
  auto max = max_value_.Load();
  if (PREDICT_FALSE(min > max)) {
    return 0;
  }
  return max;
}

double AggregateStats::MeanValue() const {
  auto count = CurrentCount();
  if (count == 0) {
    return 0.0;
  }
  return static_cast<double>(CurrentSum()) / count;
}

void AggregateStats::Reset(PreserveTotalStats preserve_total) {
  if (preserve_total) {
    int64_t previous_sum = current_sum_.Exchange(0);
    int64_t previous_count = current_count_.Exchange(0);
    total_sum_.IncrementBy(previous_sum);
    total_count_.IncrementBy(previous_count);
    aggregator_.SumIncrementBy(previous_sum);
    aggregator_.CountIncrementBy(previous_count);
  } else {
    current_sum_.Store(0);
    current_count_.Store(0);
    int64_t previous_total_sum = total_sum_.Exchange(0);
    int64_t previous_total_count = total_count_.Exchange(0);
    aggregator_.SumIncrementBy(-previous_total_sum);
    aggregator_.CountIncrementBy(-previous_total_count);
  }
  min_value_.Store(std::numeric_limits<int64_t>::max());
  max_value_.Store(std::numeric_limits<int64_t>::min());
}

void AggregateStats::Add(const AggregateStats& other) {
  current_sum_.IncrementBy(other.total_sum_.Load() + other.current_sum_.Load());
  current_count_.IncrementBy(other.total_count_.Load() + other.current_count_.Load());
  min_value_.StoreMin(other.min_value_.Load());
  max_value_.StoreMax(other.max_value_.Load());
}

Status AggregateStats::SetUpPreAggregationForPrometheus(
    std::shared_ptr<AtomicInt<int64_t>> aggregated_prometheus_sum_value_holder,
    std::shared_ptr<AtomicInt<int64_t>> aggregated_prometheus_count_value_holder) {
  RETURN_NOT_OK(aggregator_.InitializeValueHolders(
      aggregated_prometheus_sum_value_holder, aggregated_prometheus_count_value_holder));
  aggregator_.SumIncrementBy(total_sum_.Load());
  aggregator_.CountIncrementBy(total_count_.Load());
  return Status::OK();
}

bool AggregateStats::IsPreAggregatedForPrometheus() const {
  return aggregator_.HasValueHolders();
}

Status AggregateStats::Aggregator::InitializeValueHolders(
    std::shared_ptr<AtomicInt<int64_t>> aggregated_prometheus_sum_value_holder,
    std::shared_ptr<AtomicInt<int64_t>> aggregated_prometheus_count_value_holder) {
  RSTATUS_DCHECK(!HasValueHolders(), IllegalState, "Aggregator value holders are already set.");

  if (aggregated_prometheus_sum_value_holder != nullptr &&
      aggregated_prometheus_count_value_holder != nullptr) {
      sum_holder_ = std::move(aggregated_prometheus_sum_value_holder);
      count_holder_ = std::move(aggregated_prometheus_count_value_holder);
    return Status::OK();
  }

  RSTATUS_DCHECK(
      (aggregated_prometheus_sum_value_holder == nullptr &&
       aggregated_prometheus_count_value_holder == nullptr),
      IllegalState,
      "Both aggregated_prometheus_sum_value_holder and aggregated_prometheus_count_value_holder "
      "must be either nullptr or have values."
  );
  return Status::OK();
}

} // namespace yb
