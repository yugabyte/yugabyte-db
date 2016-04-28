// Copyright (c) YugaByte, Inc.

#ifndef YB_ROCKSUTIL_YB_ROCKSDB_LOGGER_H
#define YB_ROCKSUTIL_YB_ROCKSDB_LOGGER_H

#include <string>
#include "rocksdb/env.h"

namespace yb {

class YBRocksDBLogger : public rocksdb::Logger {
 public:
  explicit YBRocksDBLogger(const std::string& prefix) : prefix_(prefix) {}

  ~YBRocksDBLogger() {}

  // Write an entry to the log file with the specified format.
  void Logv(const char* format, va_list ap) override {
    Logv(rocksdb::INFO_LEVEL, format, ap);
  }

  // Write an entry to the log file with the specified log level and format. Any log with level
  // under the internal log level of *this (see @SetInfoLogLevel and @GetInfoLogLevel) will not be
  // printed.
  void Logv(const rocksdb::InfoLogLevel log_level, const char* format, va_list ap) override;

  // Convert from glog level to rocksdb log level.
  static rocksdb::InfoLogLevel ConvertToRocksDBLogLevel(const int glog_level);

 private:
  // Convert from rocksdb log level to glog level.
  static int ConvertToGLogLevel(const rocksdb::InfoLogLevel rocksdb_log_level);

  const std::string prefix_;
};

}

#endif // YB_ROCKSUTIL_YB_ROCKSDB_LOGGER_H
