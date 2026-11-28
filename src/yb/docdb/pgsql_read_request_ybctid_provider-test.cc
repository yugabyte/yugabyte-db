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

#include <algorithm>
#include <optional>
#include <string>
#include <vector>
#include <random>
#include <ranges>

#include <gtest/gtest.h>

#include "yb/common/pgsql_protocol.messages.h"
#include "yb/common/schema.h"

#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/pgsql_read_request_ybctid_provider.h"

#include "yb/gutil/stringprintf.h"

#include "yb/util/flags.h"
#include "yb/util/memory/arena.h"
#include "yb/util/status_format.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

DECLARE_bool(legacy_ybctid_batch_pagination);

namespace yb::docdb {

namespace {

const std::string kPartitionKey = "partition_key";
constexpr int64_t kOrderBase = 100;

// Action to take on a key returned by FetchKey(). kAdd means the key exists and is added to the
// result set. kNotFound means the key does not exist and is reported as missing. kPaginate means
// the key is found but can't be added to the result set.
enum class Action {
  kAdd,
  kNotFound,
  kPaginate,
};

const Schema kSchema(
    {ColumnSchema("r", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST)}, {10_ColId});

struct PageResult {
  std::vector<std::string> fetched;
  std::vector<std::string> added;
};

struct ExpectedPagingState {
  bool has_partition_key;
  std::optional<uint64_t> keys_processed;
  std::vector<uint64_t> keys_missing;
};

} // namespace

class PgsqlReadRequestYbctidProviderBase : public YBTest {
 protected:
  // Zero padded, so the byte order of the keys matches their index order.
  static std::string Key(size_t index) {
    return Format("k$0", ZeroPadded(index, 2));
  }

  static std::vector<std::string> Keys(size_t count) {
    std::vector<std::string> result;
    for (size_t i = 0; i != count; ++i) {
      result.push_back(Key(i));
    }
    return result;
  }

  std::vector<std::string> ShuffledKeys(size_t count) {
    auto result = Keys(count);
    std::shuffle(result.begin(), result.end(), rng);
    return result;
  }

  std::vector<std::string> FilterKeys(
      const std::vector<std::string>& keys, std::ranges::input_range auto&& indexes)
      requires std::unsigned_integral<std::ranges::range_value_t<decltype(indexes)>> {
    std::vector<std::string> result;
    result.reserve(indexes.size());
    for (auto index : indexes) {
      result.push_back(keys[index]);
    }
    return result;
  }

  PgsqlReadRequestMsg& NewRequest(const std::vector<std::string>& keys, bool ordered = false) {
    auto& request = *arena_.NewArenaObject<PgsqlReadRequestMsg>();
    request.dup_partition_key(kPartitionKey);
    for (size_t i = 0; i != keys.size(); ++i) {
      auto& arg = *request.add_batch_arguments();
      if (ordered) {
        arg.set_order(kOrderBase + i);
      }
      arg.mutable_ybctid()->mutable_value()->dup_binary_value(keys[i]);
    }
    return request;
  }

  PgsqlResponseMsg& NewResponse() {
    return *arena_.NewArenaObject<PgsqlResponseMsg>();
  }

  static void SetPagingState(
      PgsqlReadRequestMsg& request, uint64_t keys_processed,
      const std::vector<uint64_t>& keys_missing) {
    auto& paging_state = *request.mutable_paging_state();
    if (keys_processed > 0) {
      paging_state.set_batch_keys_processed(keys_processed);
    }
    for (auto index : keys_missing) {
      paging_state.add_batch_keys_missing(index);
    }
  }

  DocReadContext doc_read_context_ = DocReadContext::TEST_Create(kSchema);
 private:
  ThreadSafeArena arena_;
  std::mt19937 rng{42};
};

class PgsqlReadRequestYbctidProviderTest :
    public PgsqlReadRequestYbctidProviderBase, public ::testing::WithParamInterface<bool> {
 protected:
  void SetUp() override {
    PgsqlReadRequestYbctidProviderBase::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_legacy_ybctid_batch_pagination) = GetParam();
  }

  // Processes one request the way PgsqlReadOperation::ExecuteBatchKeys does. Keys not in existing
  // are reported as not found. When max_rows is set, the provider is asked to paginate on the next
  // found key after max_rows keys were added to the result set.
  Result<PageResult> RunPage(
      const PgsqlReadRequestMsg& request, PgsqlResponseMsg& response,
      const std::vector<Action>& script) {
    LOG(INFO) << "Request: " << request.ShortDebugString();
    PgsqlReadRequestYbctidProvider provider(doc_read_context_, request, response);
    PageResult result;
    auto script_it = script.begin();
    for (;;) {
      auto key = VERIFY_RESULT(provider.FetchKey());
      if (key.empty()) {
        SCHECK(script_it == script.end(), InvalidArgument, "Script has more actions than keys");
        LOG(INFO) << "Response: " << response.ShortDebugString();
        return result;
      }
      result.fetched.push_back(key.ToBuffer());
      SCHECK(script_it != script.end(), InvalidArgument, "Script has no action for key");
      switch (*script_it) {
        case Action::kNotFound:
          provider.KeyNotFound();
          break;
        case Action::kAdd:
          result.added.push_back(result.fetched.back());
          provider.AddedKeyToResultSet();
          break;
        case Action::kPaginate:
          RETURN_NOT_OK(provider.UpdatePagingState());
          LOG(INFO) << "Response: " << response.ShortDebugString();
          return result;
        default:
          return STATUS_FORMAT(InvalidArgument, "Unknown action: $0", static_cast<int>(*script_it));
      }
      ++script_it;
    }
  }

  static void CheckPageResult(
      const PageResult& page, const std::vector<std::string>& expected_fetched,
      const std::vector<std::string>& expected_added) {
    ASSERT_EQ(page.fetched, expected_fetched);
    ASSERT_EQ(page.added, expected_added);
  }

  static void CheckResponse(
      const PgsqlResponseMsg& response, int64_t expected_batch_arg_count,
      const std::vector<int64_t>& expected_orders,
      std::optional<ExpectedPagingState> expected_paging_state = std::nullopt) {
    ASSERT_EQ(response.batch_arg_count(), expected_batch_arg_count);
    if (expected_orders.empty()) {
      ASSERT_TRUE(response.batch_orders().empty());
    } else {
      std::vector<int64_t> orders(response.batch_orders().begin(), response.batch_orders().end());
      ASSERT_EQ(orders, expected_orders);
    }
    if (expected_paging_state) {
      ASSERT_TRUE(response.has_paging_state());
      if (expected_paging_state->has_partition_key) {
        ASSERT_EQ(response.paging_state().next_partition_key().ToBuffer(), kPartitionKey);
      }
      if (expected_paging_state->keys_processed) {
        ASSERT_TRUE(response.paging_state().has_batch_keys_processed());
        ASSERT_EQ(response.paging_state().batch_keys_processed(),
                  *expected_paging_state->keys_processed);
      } else {
        ASSERT_FALSE(response.paging_state().has_batch_keys_processed());
      }
      if (!expected_paging_state->keys_missing.empty()) {
        std::vector<uint64_t> missing(
            response.paging_state().batch_keys_missing().begin(),
            response.paging_state().batch_keys_missing().end());
        ASSERT_EQ(missing, expected_paging_state->keys_missing);
      } else {
        ASSERT_TRUE(response.paging_state().batch_keys_missing().empty());
      }
    } else {
      ASSERT_FALSE(response.has_paging_state());
    }
  }
};

TEST_F(PgsqlReadRequestYbctidProviderBase, BoundsCoverAllKeys) {
  const auto keys = ShuffledKeys(10);
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  PgsqlReadRequestYbctidProvider provider(doc_read_context_, request, response);
  auto bounds = provider.Bounds();
  ASSERT_EQ(bounds.first.ToBuffer(), Key(0));
  ASSERT_EQ(bounds.second.ToBuffer(), Key(9));
}

TEST_F(PgsqlReadRequestYbctidProviderBase, BoundsSkipProcessedKeys) {
  const auto keys = {Key(1), Key(9), Key(2), Key(4), Key(7), Key(3), Key(5)};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  SetPagingState(request, 2, {0});
  PgsqlReadRequestYbctidProvider provider(doc_read_context_, request, response);
  auto bounds = provider.Bounds();
  ASSERT_EQ(bounds.first.ToBuffer(), Key(2));
  ASSERT_EQ(bounds.second.ToBuffer(), Key(7));
}

TEST_F(PgsqlReadRequestYbctidProviderBase, BoundsCoverRetryKeys) {
  const auto keys = {Key(1), Key(9), Key(2), Key(4), Key(7), Key(3), Key(5)};
  const std::vector<uint64_t> missing_keys{0, 2, 4, 6};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  SetPagingState(request, 0, missing_keys);
  PgsqlReadRequestYbctidProvider provider(doc_read_context_, request, response);
  auto bounds = provider.Bounds();
  ASSERT_EQ(bounds.first.ToBuffer(), Key(1));
  ASSERT_EQ(bounds.second.ToBuffer(), Key(7));
}

TEST_F(PgsqlReadRequestYbctidProviderBase, BoundsSkipProcessedRetryKeys) {
  const auto keys = {Key(1), Key(9), Key(2), Key(4), Key(7), Key(3), Key(5)};
  const std::vector<uint64_t> missing_keys{0, 1, 3, 5};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  SetPagingState(request, 3, missing_keys);
  PgsqlReadRequestYbctidProvider provider(doc_read_context_, request, response);
  auto bounds = provider.Bounds();
  ASSERT_EQ(bounds.first.ToBuffer(), Key(3));
  ASSERT_EQ(bounds.second.ToBuffer(), Key(4));
}

// Fetch tests
INSTANTIATE_TEST_CASE_P(
    PgsqlReadRequestYbctidProvider, PgsqlReadRequestYbctidProviderTest, ::testing::Bool());

// Macros to construct expected results
#define EXPECTED_ADDED(keys, script) \
  std::vector<std::string> __added_; \
  for (auto [idx, action] : std::views::zip(std::views::iota(0), script)) { \
    if (action == Action::kAdd) { \
      __added_.push_back(keys[idx]); \
    } \
  } \
  const auto expected_added(std::move(__added_))

#define EXPECTED_FETCHED_ADDED_ORDERS(keys, span, script) \
  const auto expected_fetched = FilterKeys(keys, span); \
  std::vector<std::string> __added_; \
  std::vector<int64_t> __orders_; \
  for (auto [idx, action] : std::views::zip(span, script)) { \
    if (action == Action::kAdd) { \
      __added_.push_back(keys[idx]); \
      __orders_.push_back(kOrderBase + idx); \
    } \
  } \
  const auto expected_added(std::move(__added_)); \
  const auto expected_orders(std::move(__orders_))

// Fetch without initial paging state

// Batch at once

TEST_P(PgsqlReadRequestYbctidProviderTest, FetchAllKeysUnordered) {
  const auto keys = Keys(5);
  const std::vector<Action> script(keys.size(), Action::kAdd);
  auto& request = NewRequest(keys);
  auto& response = NewResponse();
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_ADDED(keys, script);
  CheckPageResult(page, keys, expected_added);
  CheckResponse(response, keys.size(), {});
}

TEST_P(PgsqlReadRequestYbctidProviderTest, FetchAllKeys) {
  const auto keys = ShuffledKeys(5);
  const std::vector<Action> script(keys.size(), Action::kAdd);
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::views::iota(0ULL, keys.size()), script);
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, keys.size(), expected_orders);
}

TEST_P(PgsqlReadRequestYbctidProviderTest, FetchLeadingKeysUnordered) {
  const auto keys = Keys(5);
  const std::vector<Action> script{
      Action::kAdd, Action::kAdd, Action::kAdd, Action::kNotFound, Action::kNotFound};
  auto& request = NewRequest(keys);
  auto& response = NewResponse();
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_ADDED(keys, script);
  CheckPageResult(page, keys, expected_added);
  CheckResponse(response, keys.size(), {});
}

TEST_P(PgsqlReadRequestYbctidProviderTest, FetchLeadingKeys) {
  const auto keys = ShuffledKeys(5);
  const std::vector<Action> script{
      Action::kAdd, Action::kAdd, Action::kAdd, Action::kNotFound, Action::kNotFound};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::views::iota(0ULL, keys.size()), script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = false,
    .keys_processed = std::nullopt,
    .keys_missing = {3, 4}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, keys.size(), expected_orders, expected_paging_state);
}

TEST_P(PgsqlReadRequestYbctidProviderTest, FetchTrailingKeysUnordered) {
  const auto keys = Keys(5);
  const std::vector<Action> script{
      Action::kNotFound, Action::kNotFound, Action::kAdd, Action::kAdd, Action::kAdd};
  auto& request = NewRequest(keys);
  auto& response = NewResponse();
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_ADDED(keys, script);
  CheckPageResult(page, keys, expected_added);
  CheckResponse(response, keys.size(), {});
}

TEST_P(PgsqlReadRequestYbctidProviderTest, FetchTrailingKeys) {
  const auto keys = ShuffledKeys(5);
  const std::vector<Action> script{
      Action::kNotFound, Action::kNotFound, Action::kAdd, Action::kAdd, Action::kAdd};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::views::iota(0ULL, keys.size()), script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = false,
    .keys_processed = std::nullopt,
    .keys_missing = {0, 1}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, keys.size(), expected_orders, expected_paging_state);
}

TEST_P(PgsqlReadRequestYbctidProviderTest, FetchMixedKeysUnordered) {
  const auto keys = Keys(5);
  const std::vector<Action> script{
      Action::kAdd, Action::kNotFound, Action::kAdd, Action::kNotFound, Action::kAdd};
  auto& request = NewRequest(keys);
  auto& response = NewResponse();
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_ADDED(keys, script);
  CheckPageResult(page, keys, expected_added);
  CheckResponse(response, keys.size(), {});
}

TEST_P(PgsqlReadRequestYbctidProviderTest, FetchMixedKeys) {
  const auto keys = ShuffledKeys(5);
  const std::vector<Action> script{
      Action::kNotFound, Action::kAdd, Action::kNotFound, Action::kAdd, Action::kNotFound};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::views::iota(0ULL, keys.size()), script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = false,
    .keys_processed = std::nullopt,
    .keys_missing = {0, 2, 4}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, keys.size(), expected_orders, expected_paging_state);
}

// One page of the batch

TEST_P(PgsqlReadRequestYbctidProviderTest, FetchAllKeysUnorderedPagination) {
  const auto keys = Keys(5);
  const std::vector<Action> script{
      Action::kAdd, Action::kAdd, Action::kAdd, Action::kAdd, Action::kPaginate};
  auto& request = NewRequest(keys);
  auto& response = NewResponse();
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = true,
    .keys_processed = std::nullopt,
    .keys_missing = {}};
  EXPECTED_ADDED(keys, script);
  CheckPageResult(page, keys, expected_added);
  CheckResponse(response, keys.size() - 1, {}, expected_paging_state);
}

TEST_P(PgsqlReadRequestYbctidProviderTest, FetchAllKeysPagination) {
  const auto keys = ShuffledKeys(5);
  const std::vector<Action> script{
      Action::kAdd, Action::kAdd, Action::kAdd, Action::kAdd, Action::kPaginate};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::views::iota(0ULL, keys.size()), script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = true,
    .keys_processed = std::nullopt,
    .keys_missing = {}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, keys.size() - 1, expected_orders, expected_paging_state);
}

TEST_P(PgsqlReadRequestYbctidProviderTest, FetchTrailingKeysUnorderedPagination) {
  const auto keys = Keys(5);
  const std::vector<Action> script{
      Action::kNotFound, Action::kNotFound, Action::kAdd, Action::kAdd, Action::kPaginate};
  auto& request = NewRequest(keys);
  auto& response = NewResponse();
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = true,
    .keys_processed = std::nullopt,
    .keys_missing = {}};
  EXPECTED_ADDED(keys, script);
  CheckPageResult(page, keys, expected_added);
  CheckResponse(response, keys.size() - 1, {}, expected_paging_state);
}

TEST_P(PgsqlReadRequestYbctidProviderTest, FetchTrailingKeysPagination) {
  const auto keys = ShuffledKeys(5);
  const std::vector<Action> script{
      Action::kNotFound, Action::kNotFound, Action::kAdd, Action::kAdd, Action::kPaginate};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  // In legacy mode pagination is unsafe
  if (GetParam()) {
    ASSERT_NOK(RunPage(request, response, script));
    return;
  }
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::views::iota(0ULL, keys.size()), script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = true,
    .keys_processed = 4,
    .keys_missing = {0, 1}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, 0, expected_orders, expected_paging_state);
}

TEST_P(PgsqlReadRequestYbctidProviderTest, FetchMixedKeysUnorderedPagination) {
  const auto keys = Keys(5);
  const std::vector<Action> script{
      Action::kNotFound, Action::kAdd, Action::kNotFound, Action::kAdd, Action::kPaginate};
  auto& request = NewRequest(keys);
  auto& response = NewResponse();
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = true,
    .keys_processed = std::nullopt,
    .keys_missing = {}};
  EXPECTED_ADDED(keys, script);
  CheckPageResult(page, keys, expected_added);
  CheckResponse(response, keys.size() - 1, {}, expected_paging_state);
}

TEST_P(PgsqlReadRequestYbctidProviderTest, FetchMixedKeysPagination) {
  const auto keys = ShuffledKeys(5);
  const std::vector<Action> script{
      Action::kAdd, Action::kNotFound, Action::kAdd, Action::kNotFound, Action::kPaginate};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  // In legacy mode pagination is unsafe
  if (GetParam()) {
    ASSERT_NOK(RunPage(request, response, script));
    return;
  }
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::views::iota(0ULL, keys.size()), script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = true,
    .keys_processed = 3,
    .keys_missing = {0, 2}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, 1, expected_orders, expected_paging_state);
}

// Continue fetching after first pass pagination
// Note, that the continuation paging state is only populated if there are missing keys,
// otherwise leading keys are dropped. Leading keys are dropped up to the first missing key, so
// the missing key 0 is always present. Unordered fetch does not track missing keys.

// Batch at once

TEST_P(PgsqlReadRequestYbctidProviderTest, ContinueAllKeys) {
  const uint64_t kProcessed = 2;
  const auto keys = ShuffledKeys(7);
  const std::vector<Action> script(keys.size() - kProcessed, Action::kAdd);
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  SetPagingState(request, kProcessed, {0, 1});
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::views::iota(kProcessed, keys.size()), script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = false,
    .keys_processed = std::nullopt,
    .keys_missing = {0, 1}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, keys.size(), expected_orders, expected_paging_state);
}

TEST_P(PgsqlReadRequestYbctidProviderTest, ContinueLeadingKeys) {
  const uint64_t kProcessed = 2;
  const auto keys = ShuffledKeys(7);
  const std::vector<Action> script{
      Action::kAdd, Action::kAdd, Action::kAdd, Action::kNotFound, Action::kNotFound};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  SetPagingState(request, kProcessed, {0});
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::views::iota(kProcessed, keys.size()), script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = false,
    .keys_processed = std::nullopt,
    .keys_missing = {0, 5, 6}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, keys.size(), expected_orders, expected_paging_state);
}

TEST_P(PgsqlReadRequestYbctidProviderTest, ContinueTrailingKeys) {
  const uint64_t kProcessed = 2;
  const auto keys = ShuffledKeys(7);
  const std::vector<Action> script{
      Action::kNotFound, Action::kNotFound, Action::kAdd, Action::kAdd, Action::kAdd};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  SetPagingState(request, kProcessed, {0});
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::views::iota(kProcessed, keys.size()), script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = false,
    .keys_processed = std::nullopt,
    .keys_missing = {0, 2, 3}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, keys.size(), expected_orders, expected_paging_state);
}

TEST_P(PgsqlReadRequestYbctidProviderTest, ContinueMixedKeys) {
  const uint64_t kProcessed = 2;
  const auto keys = ShuffledKeys(7);
  const std::vector<Action> script{
      Action::kAdd, Action::kNotFound, Action::kAdd, Action::kNotFound, Action::kAdd};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  SetPagingState(request, kProcessed, {0, 1});
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::views::iota(kProcessed, keys.size()), script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = false,
    .keys_processed = std::nullopt,
    .keys_missing = {0, 1, 3, 5}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, keys.size(), expected_orders, expected_paging_state);
}

// One page of the batch

TEST_P(PgsqlReadRequestYbctidProviderTest, ContinueAllKeysPagination) {
  const uint64_t kProcessed = 2;
  const auto keys = ShuffledKeys(7);
  const std::vector<Action> script{
      Action::kAdd, Action::kAdd, Action::kAdd, Action::kAdd, Action::kPaginate};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  SetPagingState(request, kProcessed, {0, 1});
  // In legacy mode pagination is unsafe
  if (GetParam()) {
    ASSERT_NOK(RunPage(request, response, script));
    return;
  }
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::views::iota(kProcessed, keys.size()), script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = true,
    .keys_processed = 6,
    .keys_missing = {0, 1}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, 0, expected_orders, expected_paging_state);
}

TEST_P(PgsqlReadRequestYbctidProviderTest, ContinueLeadingKeysPagination) {
  const uint64_t kProcessed = 2;
  const auto keys = ShuffledKeys(7);
  const std::vector<Action> script{
      Action::kAdd, Action::kAdd, Action::kNotFound, Action::kNotFound, Action::kPaginate};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  SetPagingState(request, kProcessed, {0});
  // In legacy mode pagination is unsafe
  if (GetParam()) {
    ASSERT_NOK(RunPage(request, response, script));
    return;
  }
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::views::iota(kProcessed, keys.size()), script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = true,
    .keys_processed = 6,
    .keys_missing = {0, 4, 5}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, 0, expected_orders, expected_paging_state);
}

TEST_P(PgsqlReadRequestYbctidProviderTest, ContinueTrailingKeysPagination) {
  const uint64_t kProcessed = 2;
  const auto keys = ShuffledKeys(7);
  const std::vector<Action> script{
      Action::kNotFound, Action::kNotFound, Action::kAdd, Action::kAdd, Action::kPaginate};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  SetPagingState(request, kProcessed, {0});
  // In legacy mode pagination is unsafe
  if (GetParam()) {
    ASSERT_NOK(RunPage(request, response, script));
    return;
  }
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::views::iota(kProcessed, keys.size()), script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = true,
    .keys_processed = 6,
    .keys_missing = {0, 2, 3}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, 0, expected_orders, expected_paging_state);
}

TEST_P(PgsqlReadRequestYbctidProviderTest, ContinueMixedKeysPagination) {
  const uint64_t kProcessed = 2;
  const auto keys = ShuffledKeys(7);
  const std::vector<Action> script{
      Action::kAdd, Action::kNotFound, Action::kAdd, Action::kNotFound, Action::kPaginate};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  SetPagingState(request, kProcessed, {0, 1});
  // In legacy mode pagination is unsafe
  if (GetParam()) {
    ASSERT_NOK(RunPage(request, response, script));
    return;
  }
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::views::iota(kProcessed, keys.size()), script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = true,
    .keys_processed = 6,
    .keys_missing = {0, 1, 3, 5}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, 0, expected_orders, expected_paging_state);
}

// Retry missing keys on a next tablet

// Batch at once

TEST_P(PgsqlReadRequestYbctidProviderTest, RetryAllKeys) {
  const auto keys = ShuffledKeys(7);
  const std::vector<uint64_t> missing_keys{0, 2, 4, 6};
  const std::vector<Action> script(missing_keys.size(), Action::kAdd);
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  SetPagingState(request, 0, missing_keys);
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::span{missing_keys}, script);
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, keys.size(), expected_orders);
}

TEST_P(PgsqlReadRequestYbctidProviderTest, RetryLeadingKeys) {
  const auto keys = ShuffledKeys(7);
  const std::vector<uint64_t> missing_keys{0, 2, 4, 6};
  const std::vector<Action> script{
      Action::kAdd, Action::kAdd, Action::kNotFound, Action::kNotFound};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  SetPagingState(request, 0, missing_keys);
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::span{missing_keys}, script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = false,
    .keys_processed = std::nullopt,
    .keys_missing = {4, 6}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, keys.size(), expected_orders, expected_paging_state);
}

TEST_P(PgsqlReadRequestYbctidProviderTest, RetryTrailingKeys) {
  const auto keys = ShuffledKeys(7);
  const std::vector<uint64_t> missing_keys{0, 2, 4, 6};
  const std::vector<Action> script{
      Action::kNotFound, Action::kNotFound, Action::kAdd, Action::kAdd};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  SetPagingState(request, 0, missing_keys);
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::span{missing_keys}, script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = false,
    .keys_processed = std::nullopt,
    .keys_missing = {0, 2}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, keys.size(), expected_orders, expected_paging_state);
}

TEST_P(PgsqlReadRequestYbctidProviderTest, RetryMixedKeys) {
  const auto keys = ShuffledKeys(7);
  const std::vector<uint64_t> missing_keys{0, 2, 4, 6};
  const std::vector<Action> script{
      Action::kAdd, Action::kNotFound, Action::kAdd, Action::kNotFound};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  SetPagingState(request, 0, missing_keys);
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::span{missing_keys}, script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = false,
    .keys_processed = std::nullopt,
    .keys_missing = {2, 6}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, keys.size(), expected_orders, expected_paging_state);
}

// One page of the batch

TEST_P(PgsqlReadRequestYbctidProviderTest, RetryAllKeysPagination) {
  const auto keys = ShuffledKeys(7);
  const std::vector<uint64_t> missing_keys{0, 2, 4, 6};
  const std::vector<Action> script{
      Action::kAdd, Action::kAdd, Action::kAdd, Action::kPaginate};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  SetPagingState(request, 0, missing_keys);
  // In legacy mode pagination is unsafe
  if (GetParam()) {
    ASSERT_NOK(RunPage(request, response, script));
    return;
  }
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::span{missing_keys}, script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = true,
    .keys_processed = 0,
    .keys_missing = {0}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, keys.size() - 1, expected_orders, expected_paging_state);
}

TEST_P(PgsqlReadRequestYbctidProviderTest, RetryLeadingKeysPagination) {
  const auto keys = ShuffledKeys(7);
  const std::vector<uint64_t> missing_keys{0, 2, 4, 6};
  const std::vector<Action> script{
      Action::kAdd, Action::kAdd, Action::kNotFound, Action::kPaginate};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  SetPagingState(request, 0, missing_keys);
  // In legacy mode pagination is unsafe
  if (GetParam()) {
    ASSERT_NOK(RunPage(request, response, script));
    return;
  }
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::span{missing_keys}, script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = true,
    .keys_processed = 2,
    .keys_missing = {0, 2}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, 4, expected_orders, expected_paging_state);
}

TEST_P(PgsqlReadRequestYbctidProviderTest, RetryTrailingKeysPagination) {
  const auto keys = ShuffledKeys(7);
  const std::vector<uint64_t> missing_keys{0, 2, 4, 6};
  const std::vector<Action> script{
      Action::kNotFound, Action::kNotFound, Action::kAdd, Action::kPaginate};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  SetPagingState(request, 0, missing_keys);
  // In legacy mode pagination is unsafe
  if (GetParam()) {
    ASSERT_NOK(RunPage(request, response, script));
    return;
  }
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::span{missing_keys}, script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = true,
    .keys_processed = 6,
    .keys_missing = {0, 2, 6}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, 0, expected_orders, expected_paging_state);
}

TEST_P(PgsqlReadRequestYbctidProviderTest, RetryMixedKeysPagination) {
  const auto keys = ShuffledKeys(7);
  const std::vector<uint64_t> missing_keys{0, 2, 4, 6};
  const std::vector<Action> script{
      Action::kNotFound, Action::kAdd, Action::kNotFound, Action::kPaginate};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  SetPagingState(request, 0, missing_keys);
  // In legacy mode pagination is unsafe
  if (GetParam()) {
    ASSERT_NOK(RunPage(request, response, script));
    return;
  }
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::span{missing_keys}, script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = true,
    .keys_processed = 6,
    .keys_missing = {0, 4, 6}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, 0, expected_orders, expected_paging_state);
}

// Continue retrying missing keys on a next tablet

// Batch at once

TEST_P(PgsqlReadRequestYbctidProviderTest, ContinueRetryAllKeys) {
  const uint64_t kProcessed = 2;
  const auto keys = ShuffledKeys(9);
  const std::vector<uint64_t> missing_keys{0, 2, 4, 6, 8};
  const std::vector<Action> script(missing_keys.size() - kProcessed, Action::kAdd);
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  SetPagingState(request, missing_keys[kProcessed], missing_keys);
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::span{missing_keys}.subspan(kProcessed), script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = false,
    .keys_processed = std::nullopt,
    .keys_missing = {0, 2}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, keys.size(), expected_orders, expected_paging_state);
}

TEST_P(PgsqlReadRequestYbctidProviderTest, ContinueRetryLeadingKeys) {
  const uint64_t kProcessed = 2;
  const auto keys = ShuffledKeys(9);
  const std::vector<uint64_t> missing_keys{0, 2, 4, 6, 8};
  const std::vector<Action> script{Action::kAdd, Action::kAdd, Action::kNotFound};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  SetPagingState(request, missing_keys[kProcessed], missing_keys);
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::span{missing_keys}.subspan(kProcessed), script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = false,
    .keys_processed = std::nullopt,
    .keys_missing = {0, 2, 8}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, keys.size(), expected_orders, expected_paging_state);
}

TEST_P(PgsqlReadRequestYbctidProviderTest, ContinueRetryTrailingKeys) {
  const uint64_t kProcessed = 2;
  const auto keys = ShuffledKeys(9);
  const std::vector<uint64_t> missing_keys{0, 2, 4, 6, 8};
  const std::vector<Action> script{Action::kNotFound, Action::kAdd, Action::kAdd};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  SetPagingState(request, missing_keys[kProcessed], missing_keys);
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::span{missing_keys}.subspan(kProcessed), script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = false,
    .keys_processed = std::nullopt,
    .keys_missing = {0, 2, 4}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, keys.size(), expected_orders, expected_paging_state);
}

TEST_P(PgsqlReadRequestYbctidProviderTest, ContinueRetryMixedKeys) {
  const uint64_t kProcessed = 2;
  const auto keys = ShuffledKeys(9);
  const std::vector<uint64_t> missing_keys{0, 2, 4, 6, 8};
  const std::vector<Action> script{Action::kNotFound, Action::kAdd, Action::kNotFound};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  SetPagingState(request, missing_keys[kProcessed], missing_keys);
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::span{missing_keys}.subspan(kProcessed), script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = false,
    .keys_processed = std::nullopt,
    .keys_missing = {0, 2, 4, 8}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, keys.size(), expected_orders, expected_paging_state);
}

// One page of the batch

TEST_P(PgsqlReadRequestYbctidProviderTest, ContinueRetryAllKeysPagination) {
  const uint64_t kProcessed = 2;
  const auto keys = ShuffledKeys(9);
  const std::vector<uint64_t> missing_keys{0, 2, 4, 6, 8};
  const std::vector<Action> script{Action::kAdd, Action::kAdd, Action::kPaginate};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  SetPagingState(request, missing_keys[kProcessed], missing_keys);
  // In legacy mode pagination is unsafe
  if (GetParam()) {
    ASSERT_NOK(RunPage(request, response, script));
    return;
  }
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::span{missing_keys}.subspan(kProcessed), script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = true,
    .keys_processed = 8,
    .keys_missing = {0, 2, 8}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, 0, expected_orders, expected_paging_state);
}

TEST_P(PgsqlReadRequestYbctidProviderTest, ContinueRetryLeadingKeysPagination) {
  const uint64_t kProcessed = 2;
  const auto keys = ShuffledKeys(9);
  const std::vector<uint64_t> missing_keys{0, 2, 4, 6, 8};
  const std::vector<Action> script{Action::kAdd, Action::kNotFound, Action::kPaginate};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  SetPagingState(request, missing_keys[kProcessed], missing_keys);
  // In legacy mode pagination is unsafe
  if (GetParam()) {
    ASSERT_NOK(RunPage(request, response, script));
    return;
  }
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::span{missing_keys}.subspan(kProcessed), script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = true,
    .keys_processed = 8,
    .keys_missing = {0, 2, 6, 8}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, 0, expected_orders, expected_paging_state);
}

TEST_P(PgsqlReadRequestYbctidProviderTest, ContinueRetryTrailingKeysPagination) {
  const uint64_t kProcessed = 2;
  const auto keys = ShuffledKeys(9);
  const std::vector<uint64_t> missing_keys{0, 2, 4, 6, 8};
  const std::vector<Action> script{Action::kNotFound, Action::kAdd, Action::kPaginate};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  SetPagingState(request, missing_keys[kProcessed], missing_keys);
  // In legacy mode pagination is unsafe
  if (GetParam()) {
    ASSERT_NOK(RunPage(request, response, script));
    return;
  }
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::span{missing_keys}.subspan(kProcessed), script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = true,
    .keys_processed = 8,
    .keys_missing = {0, 2, 4, 8}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, 0, expected_orders, expected_paging_state);
}

TEST_P(PgsqlReadRequestYbctidProviderTest, ContinueRetryMixedKeysPagination) {
  const uint64_t kProcessed = 1;
  const auto keys = ShuffledKeys(9);
  const std::vector<uint64_t> missing_keys{0, 2, 4, 6, 8};
  const std::vector<Action> script{
      Action::kNotFound, Action::kAdd, Action::kNotFound, Action::kPaginate};
  auto& request = NewRequest(keys, /* ordered= */ true);
  auto& response = NewResponse();
  SetPagingState(request, missing_keys[kProcessed], missing_keys);
  // In legacy mode pagination is unsafe
  if (GetParam()) {
    ASSERT_NOK(RunPage(request, response, script));
    return;
  }
  auto page = ASSERT_RESULT(RunPage(request, response, script));
  EXPECTED_FETCHED_ADDED_ORDERS(keys, std::span{missing_keys}.subspan(kProcessed), script);
  const ExpectedPagingState expected_paging_state{
    .has_partition_key = true,
    .keys_processed = 8,
    .keys_missing = {0, 2, 6, 8}};
  CheckPageResult(page, expected_fetched, expected_added);
  CheckResponse(response, 0, expected_orders, expected_paging_state);
}

} // namespace yb::docdb
