// Copyright (c) YugaByte, Inc.

#include <glog/logging.h>

#include "yb/rocksutil/yb_rocksdb_logger.h"

using namespace rocksdb;

namespace yb {

void YBRocksDBLogger::Logv(const rocksdb::InfoLogLevel log_level,
                           const char *format,
                           va_list ap) {
  // Any log messages longer than 1024 will get truncated. The user is responsible for chopping
  // longer messages into multiple lines.
  static constexpr int kBufferSize = 1024;
  char buffer[kBufferSize];

  va_list backup_ap;
  va_copy(backup_ap, ap);
  vsnprintf(buffer, kBufferSize, format, backup_ap);
  va_end(backup_ap);

  google::LogMessage(__FILE__, __LINE__, YBRocksDBLogger::ConvertToGLogLevel(log_level)).stream()
      << prefix_ << buffer;
}

int YBRocksDBLogger::ConvertToGLogLevel(const rocksdb::InfoLogLevel rocksdb_log_level) {
  switch (rocksdb_log_level) {
    case InfoLogLevel::DEBUG_LEVEL:
    case InfoLogLevel::INFO_LEVEL:
    case InfoLogLevel::HEADER_LEVEL:
      // GLOG doesn't have separate levels for DEBUG or HEADER. Default those to INFO also.
      return google::GLOG_INFO;
    case InfoLogLevel::WARN_LEVEL:
      return google::GLOG_WARNING;
    case InfoLogLevel::ERROR_LEVEL:
      return google::GLOG_ERROR;
    case InfoLogLevel::FATAL_LEVEL:
      return google::GLOG_FATAL;
    default:
      LOG(FATAL) << "Unknown rocksdb log level: " << rocksdb_log_level;
  }
}

rocksdb::InfoLogLevel YBRocksDBLogger::ConvertToRocksDBLogLevel(const int glog_level) {
  switch (glog_level) {
    case google::GLOG_INFO:
      return InfoLogLevel::INFO_LEVEL;
    case google::GLOG_WARNING:
      return InfoLogLevel::WARN_LEVEL;
    case google::GLOG_ERROR:
      return InfoLogLevel::ERROR_LEVEL;
    case google::GLOG_FATAL:
      return InfoLogLevel::FATAL_LEVEL;
    default:
      LOG(FATAL) << "Unknown glog level: " << glog_level;
  }
}

}
