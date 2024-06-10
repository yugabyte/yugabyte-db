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

#include "yb/rocksutil/yb_rocksdb_logger.h"

#include <stdarg.h>

#include "yb/util/logging.h"

using rocksdb::InfoLogLevel;

namespace yb {

void YBRocksDBLogger::Logv(const rocksdb::InfoLogLevel log_level,
                                      const char *format,
                                      va_list ap) {
  LogvWithContext(__FILE__, __LINE__, log_level, format, ap);
}

void YBRocksDBLogger::LogvWithContext(const char* file,
    const int line,
    const rocksdb::InfoLogLevel log_level,
    const char* format,
    va_list ap) {
  // We try first to use 1024-bytes buffer on stack and then dynamically allocated buffer of larger
  // size.
  static constexpr size_t kInitialBufferSize = 1024;
  static const size_t kMaxBufferSize = google::LogMessage::kMaxLogMessageLen;

  char initial_buffer[kInitialBufferSize];
  std::unique_ptr<char[]> dynamic_buffer;
  char* buffer = initial_buffer;

  auto print = [&buffer, &ap, &format](size_t buffer_size) -> size_t {
    va_list backup_ap;
    va_copy(backup_ap, ap);
    const size_t size = vsnprintf(buffer, buffer_size, format, backup_ap);
    va_end(backup_ap);
    return size;
  };

  const size_t required_buffer_size = print(kInitialBufferSize) + 1;
  if (required_buffer_size > kInitialBufferSize) {
    const size_t buffer_size = std::min(required_buffer_size, kMaxBufferSize);
    dynamic_buffer.reset(buffer = new char[buffer_size]);
    print(buffer_size);
  }

  google::LogMessage(file, line, YBRocksDBLogger::ConvertToGLogLevel(log_level)).stream()
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

} //  namespace yb
