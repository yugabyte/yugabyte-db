//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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

#include <string>

#include "yb/rocksdb/util/event_logger.h"
#include <gtest/gtest.h>
#include "yb/rocksdb/env.h"

#include "yb/rocksdb/util/testutil.h"

namespace rocksdb {

class EventLoggerTest : public RocksDBTest {};

class StringLogger : public Logger {
 public:
  using Logger::Logv;
  void Logv(const char* format, va_list ap) override {
    vsnprintf(buffer_, sizeof(buffer_), format, ap);
  }
  char* buffer() { return buffer_; }

 private:
  char buffer_[1000];
};

TEST_F(EventLoggerTest, SimpleTest) {
  StringLogger logger;
  EventLogger event_logger(&logger);
  event_logger.Log() << "id" << 5 << "event"
                     << "just_testing";
  std::string output(logger.buffer());
  ASSERT_TRUE(output.find("\"event\": \"just_testing\"") != std::string::npos);
  ASSERT_TRUE(output.find("\"id\": 5") != std::string::npos);
  ASSERT_TRUE(output.find("\"time_micros\"") != std::string::npos);
}

}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
