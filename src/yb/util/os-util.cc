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
// Imported from Impala. Changes include:
// - Namespace and imports.
// - Replaced GetStrErrMsg with ErrnoToString.
// - Replaced StringParser with strings/numbers.
// - Fixes for cpplint.
// - Fixed parsing when thread names have spaces.
#include "yb/util/os-util.h"

#include <fstream>
#include <string>
#include <vector>

#include "yb/gutil/strings/split.h"
#include "yb/util/errno.h"

using std::ifstream;
using std::istreambuf_iterator;
using std::stringstream;
using std::string;
using std::vector;
using strings::Split;
using strings::Substitute;

namespace yb {

// Ensure that Impala compiles on earlier kernels. If the target kernel does not support
// _SC_CLK_TCK, sysconf(_SC_CLK_TCK) will return -1.
#ifndef _SC_CLK_TCK
#define _SC_CLK_TCK 2
#endif

static const int64_t TICKS_PER_SEC = sysconf(_SC_CLK_TCK);

// Offsets into the ../stat file array of per-thread statistics.
//
// They are themselves offset by two because the pid and comm fields of the
// file are parsed separately.
static const int64_t USER_TICKS = 13 - 2;
static const int64_t KERNEL_TICKS = 14 - 2;
static const int64_t IO_WAIT = 41 - 2;

// Largest offset we are interested in, to check we get a well formed stat file.
static const int64_t MAX_OFFSET = IO_WAIT;

Status ParseStat(const std::string& buffer, std::string* name, ThreadStats* stats) {
  DCHECK(stats != nullptr);

  // The thread name should be the only field with parentheses. But the name
  // itself may contain parentheses.
  size_t open_paren = buffer.find('(');
  size_t close_paren = buffer.rfind(')');
  if (open_paren == string::npos  ||      // '(' must exist
      close_paren == string::npos ||      // ')' must exist
      open_paren >= close_paren   ||      // '(' must come before ')'
      close_paren + 2 == buffer.size()) { // there must be at least two chars after ')'
    return STATUS(IOError, "Unrecognised /proc format");
  }
  string extracted_name = buffer.substr(open_paren + 1, close_paren - (open_paren + 1));
  string rest = buffer.substr(close_paren + 2);
  vector<string> splits = Split(rest, " ", strings::SkipEmpty());
  if (splits.size() < MAX_OFFSET) {
    return STATUS(IOError, "Unrecognised /proc format");
  }

  int64 tmp;
  if (safe_strto64(splits[USER_TICKS], &tmp)) {
    stats->user_ns = tmp * (1e9 / TICKS_PER_SEC);
  }
  if (safe_strto64(splits[KERNEL_TICKS], &tmp)) {
    stats->kernel_ns = tmp * (1e9 / TICKS_PER_SEC);
  }
  if (safe_strto64(splits[IO_WAIT], &tmp)) {
    stats->iowait_ns = tmp * (1e9 / TICKS_PER_SEC);
  }
  if (name != nullptr) {
    *name = extracted_name;
  }
  return Status::OK();

}

Status GetThreadStats(int64_t tid, ThreadStats* stats) {
  DCHECK(stats != nullptr);
  if (TICKS_PER_SEC <= 0) {
    return STATUS(NotSupported, "ThreadStats not supported");
  }

  stringstream proc_path;
  proc_path << "/proc/self/task/" << tid << "/stat";
  ifstream proc_file(proc_path.str().c_str());
  if (!proc_file.is_open()) {
    return STATUS(IOError, "Could not open ifstream");
  }

  string buffer((istreambuf_iterator<char>(proc_file)),
      istreambuf_iterator<char>());

  return ParseStat(buffer, nullptr, stats); // don't want the name
}

bool RunShellProcess(const string& cmd, string* msg) {
  DCHECK(msg != nullptr);
  FILE* fp = popen(cmd.c_str(), "r");
  if (fp == nullptr) {
    *msg = Substitute("Failed to execute shell cmd: '$0', error was: $1", cmd,
        ErrnoToString(errno));
    return false;
  }
  // Read the first 1024 bytes of any output before pclose() so we have some idea of what
  // happened on failure.
  char buf[1024];
  size_t len = fread(buf, 1, 1024, fp);
  string output;
  output.assign(buf, len);

  // pclose() returns an encoded form of the sub-process' exit code.
  int status = pclose(fp);
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    *msg = output;
    return true;
  }

  *msg = Substitute("Shell cmd: '$0' exited with an error: '$1'. Output was: '$2'", cmd,
      ErrnoToString(errno), output);
  return false;
}

} // namespace yb
