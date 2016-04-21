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
// Base test class, with various utility functions.
#ifndef YB_UTIL_TEST_UTIL_H
#define YB_UTIL_TEST_UTIL_H

#include <gtest/gtest.h>
#include <string>

#include "yb/gutil/gscoped_ptr.h"
#include "yb/util/env.h"
#include "yb/util/test_macros.h"

namespace yb {

class YBTest : public ::testing::Test {
 public:
  YBTest();

  // Env passed in from subclass, for tests that run in-memory.
  explicit YBTest(Env *env);

  virtual ~YBTest();

  virtual void SetUp() OVERRIDE;

 protected:
  // Returns absolute path based on a unit test-specific work directory, given
  // a relative path. Useful for writing test files that should be deleted after
  // the test ends.
  std::string GetTestPath(const std::string& relative_path);

  gscoped_ptr<Env> env_;
  google::FlagSaver flag_saver_;  // Reset flags on every test.

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

    for (int i = smaller_vector->size();
         i < min(smaller_vector->size() + 16, bigger_vector->size());
         ++i) {
      LOG(WARNING) << bigger_vector_desc << "[" << i << "]: " << (*bigger_vector)[i];
    }
  }
  int num_differences_logged = 0;
  size_t num_differences_left = 0;
  size_t min_size = min(expected.size(), actual.size());
  for (int i = 0; i < min_size; ++i) {
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

} // namespace yb
#endif
