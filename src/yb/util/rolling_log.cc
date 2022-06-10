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
#include "yb/util/rolling_log.h"

#include <zlib.h>

#include <iomanip>
#include <string>

#include "yb/gutil/casts.h"
#include "yb/gutil/strings/numbers.h"
#include "yb/gutil/walltime.h"
#include "yb/util/env.h"
#include "yb/util/net/net_util.h"
#include "yb/util/path_util.h"
#include "yb/util/result.h"
#include "yb/util/status_log.h"
#include "yb/util/user.h"

using std::ostringstream;
using std::setw;
using std::string;
using strings::Substitute;

static const int kDefaultSizeLimitBytes = 64 * 1024 * 1024; // 64MB

namespace yb {

RollingLog::RollingLog(Env* env, string log_dir, string log_name)
    : env_(env),
      log_dir_(std::move(log_dir)),
      log_name_(std::move(log_name)),
      size_limit_bytes_(kDefaultSizeLimitBytes),
      compress_after_close_(true) {}

RollingLog::~RollingLog() {
  WARN_NOT_OK(Close(), "Unable to close RollingLog");
}

void RollingLog::SetSizeLimitBytes(size_t size) {
  CHECK_GT(size, 0);
  size_limit_bytes_ = size;
}

void RollingLog::SetCompressionEnabled(bool compress) {
  compress_after_close_ = compress;
}

string RollingLog::GetLogFileName(int sequence) const {
  ostringstream str;

  // 1. Program name.
  str << google::ProgramInvocationShortName();

  // 2. Host name.
  string hostname;
  Status s = GetHostname(&hostname);
  if (!s.ok()) {
    hostname = "unknown_host";
  }
  str << "." << hostname;

  // 3. User name.
  auto user_name = GetLoggedInUser();
  str << "." << (user_name.ok() ? *user_name : "unknown_user");

  // 4. Log name.
  str << "." << log_name_;

  // 5. Timestamp.
  // Implementation cribbed from glog/logging.cc
  time_t time = static_cast<time_t>(WallTime_Now());
  struct ::tm tm_time;
  localtime_r(&time, &tm_time);

  str << ".";
  str.fill('0');
  str << 1900+tm_time.tm_year
      << setw(2) << 1+tm_time.tm_mon
      << setw(2) << tm_time.tm_mday
      << '-'
      << setw(2) << tm_time.tm_hour
      << setw(2) << tm_time.tm_min
      << setw(2) << tm_time.tm_sec;
  str.clear(); // resets formatting flags

  // 6. Sequence number.
  str << "." << sequence;

  // 7. Pid.
  str << "." << getpid();

  return str.str();
}

Status RollingLog::Open() {
  CHECK(!file_);

  for (int sequence = 0; ; sequence++) {

    string path = JoinPathSegments(log_dir_,
                                   GetLogFileName(sequence));

    WritableFileOptions opts;
    // Logs aren't worth the performance cost of durability.
    opts.sync_on_close = false;
    opts.mode = Env::CREATE_NON_EXISTING;

    Status s = env_->NewWritableFile(opts, path, &file_);
    if (s.IsAlreadyPresent()) {
      // We already rolled once at this same timestamp.
      // Try again with a new sequence number.
      continue;
    }
    RETURN_NOT_OK(s);

    VLOG(1) << "Rolled " << log_name_ << " log to new file: " << path;
    break;
  }
  return Status::OK();
}

Status RollingLog::Close() {
  if (!file_) {
    return Status::OK();
  }
  string path = file_->filename();
  RETURN_NOT_OK_PREPEND(file_->Close(),
                        Substitute("Unable to close $0", path));
  file_.reset();
  if (compress_after_close_) {
    WARN_NOT_OK(CompressFile(path), "Unable to compress old log file");
  }
  return Status::OK();
}

Status RollingLog::Append(GStringPiece s) {
  if (!file_) {
    RETURN_NOT_OK_PREPEND(Open(), "Unable to open log");
  }

  if (file_->Size() + s.size() > size_limit_bytes_) {
    RETURN_NOT_OK_PREPEND(Close(), "Unable to roll log");
    RETURN_NOT_OK_PREPEND(Open(), "Unable to roll log");
  }
  RETURN_NOT_OK(file_->Append(s));
  return Status::OK();
}

namespace {

Status GzClose(gzFile f) {
  int err = gzclose(f);
  switch (err) {
    case Z_OK:
      return Status::OK();
    case Z_STREAM_ERROR:
      return STATUS(InvalidArgument, "Stream not valid");
    case Z_ERRNO:
      return STATUS(IOError, "IO Error closing stream");
    case Z_MEM_ERROR:
      return STATUS(RuntimeError, "Out of memory");
    case Z_BUF_ERROR:
      return STATUS(IOError, "read ended in the middle of a stream");
    default:
      return STATUS(IOError, "Unknown zlib error", SimpleItoa(err));
  }
}

class ScopedGzipCloser {
 public:
  explicit ScopedGzipCloser(gzFile f)
    : file_(f) {
  }

  ~ScopedGzipCloser() {
    if (file_) {
      WARN_NOT_OK(GzClose(file_), "Unable to close gzip stream");
    }
  }

  void Cancel() {
    file_ = nullptr;
  }

 private:
  gzFile file_;
};
} // anonymous namespace

// We implement CompressFile() manually using zlib APIs rather than forking
// out to '/bin/gzip' since fork() can be expensive on processes that use a large
// amount of memory. During the time of the fork, other threads could end up
// blocked. Implementing it using the zlib stream APIs isn't too much code
// and is less likely to be problematic.
Status RollingLog::CompressFile(const std::string& path) const {
  std::unique_ptr<SequentialFile> in_file;
  RETURN_NOT_OK_PREPEND(env_->NewSequentialFile(path, &in_file),
                        "Unable to open input file to compress");

  string gz_path = path + ".gz";
  gzFile gzf = gzopen(gz_path.c_str(), "w");
  if (!gzf) {
    return STATUS(IOError, "Unable to open gzip stream");
  }

  ScopedGzipCloser closer(gzf);

  // Loop reading data from the input file and writing to the gzip stream.
  uint8_t buf[32 * 1024];
  while (true) {
    Slice result;
    RETURN_NOT_OK_PREPEND(in_file->Read(arraysize(buf), &result, buf),
                          "Unable to read from gzip input");
    if (result.size() == 0) {
      break;
    }
    int n = gzwrite(gzf, result.data(), narrow_cast<unsigned>(result.size()));
    if (n == 0) {
      int errnum;
      return STATUS(IOError, "Unable to write to gzip output",
                             gzerror(gzf, &errnum));
    }
  }
  closer.Cancel();
  RETURN_NOT_OK_PREPEND(GzClose(gzf),
                        "Unable to close gzip output");

  WARN_NOT_OK(env_->DeleteFile(path),
              "Unable to delete gzip input file after compression");
  return Status::OK();
}

} // namespace yb
