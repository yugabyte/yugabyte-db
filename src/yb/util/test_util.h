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
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
// Base test class, with various utility functions.
#pragma once

#include <dirent.h>

#include <atomic>
#include <string>

#include <gtest/gtest.h>

#include "yb/util/enums.h"
#include "yb/util/env.h"
#include "yb/util/monotime.h"
#include "yb/util/port_picker.h"
#include "yb/util/logging.h"
#include "yb/util/test_macros.h" // For convenience

#define ASSERT_EVENTUALLY(expr) do { \
  AssertEventually(expr); \
  NO_PENDING_FATALS(); \
} while (0)

namespace yb {
namespace rpc {

class Messenger;

} // namespace rpc

// Our test string literals contain "\x00" that is treated as a C-string null-terminator.
// So we need to call the std::string constructor that takes the length argument.
#define BINARY_STRING(s) std::string((s), sizeof(s) - 1)

class YBTest : public ::testing::Test {
 public:
  YBTest();

  // Env passed in from subclass, for tests that run in-memory.
  explicit YBTest(Env *env);

  virtual ~YBTest();

  virtual void SetUp() override;

 protected:
  // Returns absolute path based on a unit test-specific work directory, given
  // a relative path. Useful for writing test files that should be deleted after
  // the test ends.
  std::string GetTestPath(const std::string& relative_path);

  uint16_t AllocateFreePort() { return port_picker_.AllocateFreePort(); }

  std::unique_ptr<Env> env_;
  google::FlagSaver flag_saver_;  // Reset flags on every test.
  PortPicker port_picker_;

 private:
  std::string test_dir_;
};

// Returns true if slow tests are runtime-enabled.
bool AllowSlowTests();

// Override the given gflag to the new value, only in the case that
// slow tests are enabled and the user hasn't otherwise overridden
// it on the command line.
// Example usage:
//
// OverrideFlagForSlowTests(
//     "client_inserts_per_thread",
//     strings::Substitute("$0", FLAGS_client_inserts_per_thread * 100));
//
void OverrideFlagForSlowTests(const std::string& flag_name,
                              const std::string& new_value);

// Call srand() with a random seed based on the current time, reporting
// that seed to the logs. The time-based seed may be overridden by passing
// --test_random_seed= from the CLI in order to reproduce a failed randomized
// test. Returns the seed.
int SeedRandom();

// Return a per-test directory in which to store test data. Guaranteed to
// return the same directory every time for a given unit test.
//
// May only be called from within a gtest unit test.
std::string GetTestDataDirectory();

// Wait until 'f()' succeeds without adding any GTest 'fatal failures'.
// For example:
//
//   AssertEventually([]() {
//     ASSERT_GT(ReadValueOfMetric(), 10);
//   });
//
// The function is run in a loop with exponential backoff, capped at once
// a second.
//
// To check whether AssertEventually() eventually succeeded, call
// NO_PENDING_FATALS() afterward, or use ASSERT_EVENTUALLY() which performs
// this check automatically.
void AssertEventually(const std::function<void(void)>& f,
                      const MonoDelta& timeout = MonoDelta::FromSeconds(30));

// Logs some of the differences between the two given vectors. This can be used immediately before
// asserting that two vectors are equal to make debugging easier.
template<typename T>
void LogVectorDiff(const std::vector<T>& expected, const std::vector<T>& actual) {
  if (expected.size() != actual.size()) {
    LOG(WARNING) << "Expected size: " << expected.size() << ", actual size: " << actual.size();
    const std::vector<T> *bigger_vector, *smaller_vector;
    const char *bigger_vector_desc;
    if (expected.size() > actual.size()) {
      bigger_vector = &expected;
      bigger_vector_desc = "expected";
      smaller_vector = &actual;
    } else {
      bigger_vector = &actual;
      bigger_vector_desc = "actual";
      smaller_vector = &expected;
    }

    for (auto i = smaller_vector->size();
         i < std::min(smaller_vector->size() + 16, bigger_vector->size());
         ++i) {
      LOG(WARNING) << bigger_vector_desc << "[" << i << "]: " << (*bigger_vector)[i];
    }
  }
  int num_differences_logged = 0;
  size_t num_differences_left = 0;
  size_t min_size = std::min(expected.size(), actual.size());
  for (size_t i = 0; i < min_size; ++i) {
    if (expected[i] != actual[i]) {
      if (num_differences_logged < 16) {
        LOG(WARNING) << "expected[" << i << "]: " << expected[i];
        LOG(WARNING) << "actual  [" << i << "]: " << actual[i];
        ++num_differences_logged;
      } else {
        ++num_differences_left;
      }
    }
  }
  if (num_differences_left > 0) {
    if (expected.size() == actual.size()) {
      LOG(WARNING) << num_differences_left << " more differences omitted";
    } else {
      LOG(WARNING) << num_differences_left << " more differences in the first " << min_size
      << " elements omitted";
    }
  }
}

// Return the path of a yb-tool.
std::string GetToolPath(const std::string& rel_path, const std::string& tool_name);

inline std::string GetToolPath(const std::string& tool_name) {
  return GetToolPath("../bin", tool_name);
}

inline std::string GetPgToolPath(const std::string& tool_name) {
  return GetToolPath("../postgres/bin", tool_name);
}

// For now this assumes that YB Controller binaries are present in build/ybc.
inline std::string GetYbcToolPath(const std::string& tool_name) {
  return GetToolPath("../../ybc", tool_name);
}

std::string GetCertsDir();

// Read YB_TEST_YB_CONTROLLER from env.
// If true, spawn YBC servers for backup operations.
bool UseYbController();

/*
Returns true if YB_DISABLE_MINICLUSTER_TESTS is set true.
We disable the Minicluster backup tests when we use YB Controller for backups.
This is because the varz endpoint in MiniTabletServer is not functional currently which causes the
backups to fail.
TODO: Re-enable the tests once GH#21689 is done.
*/
bool DisableMiniClusterBackupTests();

void AddExtraFlagsFromEnvVar(const char* env_var_name, std::vector<std::string>* args_dest);

int CalcNumTablets(size_t num_tablet_servers);

template<uint32_t limit>
struct LengthLimitedStringPrinter {
  explicit LengthLimitedStringPrinter(const std::string& str_)
      : str(str_) {
  }
  const std::string& str;
};

using Max500CharsPrinter = LengthLimitedStringPrinter<500>;

template<uint32_t limit>
std::ostream& operator<<(std::ostream& os, const LengthLimitedStringPrinter<limit>& printer) {
  const auto& s = printer.str;
  if (s.length() <= limit) {
    return os << s;
  }
  return os.write(s.c_str(), limit) << "... (" << (s.length() - limit) << " more characters)";
}

class StopOnFailure {
 public:
  explicit StopOnFailure(std::atomic<bool>* stop) : stop_(*stop) {}

  StopOnFailure(const StopOnFailure&) = delete;
  void operator=(const StopOnFailure&) = delete;

  ~StopOnFailure() {
    if (!success_) {
      stop_.store(true, std::memory_order_release);
    }
  }

  void Success() {
    success_ = true;
  }
 private:
  bool success_ = false;
  std::atomic<bool>& stop_;
};

YB_DEFINE_ENUM(CorruptionType, (kZero)(kXor55));

// Corrupt bytes_to_corrupt bytes at specified offset. If offset is negative, treats it as
// an offset relative to the end of file. Also fixes specified region to not exceed the file before
// corrupting data.
Status CorruptFile(
    const std::string& file_path, int64_t offset, size_t bytes_to_corrupt,
    CorruptionType corruption_type);

} // namespace yb

// Gives ability to define custom parent class for test fixture.
#define TEST_F_EX(test_case_name, test_name, parent_class) \
  GTEST_TEST_(test_case_name, test_name, parent_class, \
              ::testing::internal::GetTypeId<test_case_name>())
