// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include "yb/util/net/dns_resolver.h"

#include <fstream>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include "yb/gutil/strings/util.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/env.h"
#include "yb/util/metrics.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/scope_exit.h"
#include "yb/util/signal_util.h"
#include "yb/util/status_log.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/thread.h"

using namespace std::literals;

namespace yb {

namespace {

constexpr auto kTaskDir = "/proc/self/task";

Result<std::unordered_set<std::string>> ListThreads() {
  auto tids = VERIFY_RESULT(Env::Default()->GetChildren(kTaskDir, ExcludeDots::kTrue));
  return std::unordered_set<std::string>(tids.begin(), tids.end());
}

Result<uint64_t> BlockedSignals(const std::string& tid) {
  std::ifstream status(Format("$0/$1/status", kTaskDir, tid));
  std::string line;
  while (std::getline(status, line)) {
    if (line.starts_with("SigBlk:")) {
      return std::stoull(line.substr(line.find_first_not_of(" \t", 7)), nullptr, 16);
    }
  }
  return STATUS_FORMAT(NotFound, "No SigBlk for thread $0", tid);
}

} // namespace

class DnsResolverTest : public YBTest {
 protected:
  void SetUp() override {
    io_thread_ = CHECK_RESULT(Thread::Make("io_thread", "io_thread", [this] {
      boost::system::error_code ec;
      io_service_.run(ec);
      LOG_IF(DFATAL, ec) << "Failed to run io service: " << ec;
    }));
  }

  void TearDown() override {
    work_.reset();
    Join();
  }

 protected:
  void Join() {
    auto deadline = std::chrono::steady_clock::now() + 15s;
    while (!io_service_.stopped()) {
      if (std::chrono::steady_clock::now() >= deadline) {
        LOG(DFATAL) << "Io service failed to stop";
        io_service_.stop();
        break;
      }
      std::this_thread::sleep_for(10ms);
    }
    io_thread_->Join();
  }

  ThreadPtr io_thread_;
  IoService io_service_;
  std::optional<IoService::work> work_{io_service_};

  DnsResolver resolver_{&io_service_, nullptr};
};

TEST_F(DnsResolverTest, TestResolution) {
  auto future = resolver_.ResolveFuture("localhost");
  ASSERT_EQ(future.wait_for(1s), std::future_status::ready);
  auto addr = ASSERT_RESULT(Copy(future.get()));
  LOG(INFO) << "Address: " << addr;
  if (addr.is_v4()) {
    EXPECT_TRUE(HasPrefixString(ToString(addr), "127."));
  } else if (addr.is_v6()) {
    EXPECT_TRUE(HasPrefixString(ToString(addr), "[::1]"));
  }
}

TEST_F(DnsResolverTest, ResolverThreadBlocksYsqlSignals) {
  auto old_mask = ASSERT_RESULT(ThreadSignalMaskUnblock(kYsqlHandledSignals));
  auto se = ScopeExit([&old_mask] { CHECK_OK(ThreadSignalMaskRestore(old_mask)); });
  auto threads_before = ASSERT_RESULT(ListThreads());

  auto future = resolver_.ResolveFuture("localhost");
  ASSERT_EQ(future.wait_for(1s), std::future_status::ready);
  ASSERT_RESULT(Copy(future.get()));

  size_t new_threads = 0;
  for (const auto& tid : ASSERT_RESULT(ListThreads())) {
    if (threads_before.contains(tid)) {
      continue;
    }
    ++new_threads;
    auto blocked = ASSERT_RESULT(BlockedSignals(tid));
    for (int sig : kYsqlHandledSignals) {
      EXPECT_TRUE(blocked & (1ULL << (sig - 1)))
          << "Thread " << tid << " does not block " << strsignal(sig);
    }
  }
  EXPECT_GT(new_threads, 0);
}

} // namespace yb
