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

#pragma once

#include <string>
#include "yb/rocksdb/env.h"

namespace yb {

class YBRocksDBLogger : public rocksdb::Logger {
 public:
  explicit YBRocksDBLogger(const std::string& prefix) : prefix_(prefix) {}

  ~YBRocksDBLogger() {}

  // Write an entry to the log file with the specified format.
  void Logv(const char* format, va_list ap) override {
    Logv(rocksdb::INFO_LEVEL, format, ap);
  }

  void Logv(const rocksdb::InfoLogLevel log_level, const char* format, va_list ap) override;

  // Write an entry to the log file with the specified log level and format. Any log with level
  // under the internal log level of *this (see @SetInfoLogLevel and @GetInfoLogLevel) will not be
  // printed.
  void LogvWithContext(const char* file,
                       const int line,
                       const rocksdb::InfoLogLevel log_level,
                       const char* format,
                       va_list ap) override;

  // Convert from glog level to rocksdb log level.
  static rocksdb::InfoLogLevel ConvertToRocksDBLogLevel(const int glog_level);

  const std::string& Prefix() const override {
    return prefix_;
  }

 private:
  // Convert from rocksdb log level to glog level.
  static int ConvertToGLogLevel(const rocksdb::InfoLogLevel rocksdb_log_level);

  const std::string prefix_;
};

} // namespace yb
