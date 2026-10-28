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

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>

#include <gtest/gtest.h>

#include "yb/rpc/io_thread_pool.h"
#include "yb/rpc/scheduler.h"

#include "yb/tserver/session_registry.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/test_util.h"

using namespace std::literals;

namespace yb::tserver {
namespace {

struct SessionMetrics {
  std::atomic<size_t> start_shutdown_count{0};
  std::atomic<size_t> complete_shutdown_count{0};
};

class MockClientSession : public ClientSession {
 public:
  MockClientSession(uint64_t id, SessionMetrics& metrics) : id_(id), metrics_(metrics) {
    RenewExpiration();
  }

  uint64_t id() const override { return id_; }

  CoarseTimePoint expiration() const override { return expiration_; }

  void Touch() override {
    RenewExpiration();
  }

  void SetExpiration(CoarseTimePoint value) override { expiration_ = value; }

  void StartShutdown(bool service_shutting_down) override {
    ++metrics_.start_shutdown_count;
  }

  bool ReadyToShutdown() const override { return false; }

  void CompleteShutdown() override {
    ++metrics_.complete_shutdown_count;
  }

 private:
  void RenewExpiration() {
    SetExpiration(CoarseMonoClock::now() + 60s);
  }

  const uint64_t id_;
  CoarseTimePoint expiration_;
  SessionMetrics& metrics_;
};

class SessionRegistryTest : public YBTest {
  class Context : public SessionRegistryContext {
   public:
    Context() : pool_("", 1), scheduler_(&pool_.io_service()), registry_(&scheduler_, this) {}

    virtual ~Context() {
      registry_.Shutdown();
      scheduler_.Shutdown();
      pool_.Shutdown();
      pool_.Join();
    }

    auto& registry() { return registry_; }

   private:
    rpc::IoThreadPool pool_;
    rpc::Scheduler scheduler_;
    SessionRegistry<MockClientSession> registry_;
  };

 protected:
  void SetUp() override {
    context_.emplace();
  }

  void TearDown() override {
    context_.reset();
  }

  auto& registry() { return context_->registry(); }

 private:
  std::optional<Context> context_;
};

} // namespace

// The test checks CompleteShutdown is called on expired session during registry shutdown.
TEST_F(SessionRegistryTest, ExpiredSessionCompleteShutdownOnRegistryShutdown) {
  auto& reg = registry();
  const auto session_id = reg.NewSessionId();
  SessionMetrics metrics;
  ASSERT_OK(reg.Insert(std::make_shared<MockClientSession>(session_id, metrics)));

  reg.Expire(session_id);
  ASSERT_OK(WaitFor(
      [&metrics]() { return metrics.start_shutdown_count > 0; }, 30s, "Session Shutdown started"));
  ASSERT_EQ(metrics.start_shutdown_count, 1);
  ASSERT_EQ(metrics.complete_shutdown_count, 0);

  ASSERT_TRUE(reg.Shutdown());
  ASSERT_EQ(metrics.start_shutdown_count, 1);
  ASSERT_EQ(metrics.complete_shutdown_count, 1);
}

} // namespace yb::tserver
