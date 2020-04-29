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

#include "yb/util/ulimit_info.h"

#include <sys/resource.h>
#include <string>
#include <sstream>

#include "yb/util/env.h"

using std::string;
using std::stringstream;

namespace yb {

static stringstream& getLimit(
    stringstream& ss, const char* pfx, const char* sfx, int resource, int rightshift) {
  ss << "ulimit: " << pfx << " ";
  int64_t cur, max;
  Status status = Env::Default()->GetUlimit(resource, &cur, &max);
  if (status.ok()) {
    if (cur == RLIM_INFINITY) {
      ss << "unlimited";
    }
    else {
      ss << (cur >> rightshift);
    }
    ss << "(";
    if (max == RLIM_INFINITY) {
      ss << "unlimited";
    }
    else {
      ss << (max >> rightshift);
    }
    ss << ")";
  } else {
    ss << "-1";
  }
  ss << (sfx[0] ? " " : "") << sfx << "\n";

  return ss;
}

string UlimitInfo::GetUlimitInfo() {
  stringstream ss;
  ss << "\n";
  getLimit(ss, "core file size", "blks", RLIMIT_CORE, 0);
  getLimit(ss, "data seg size", "kb", RLIMIT_DATA, 10);
  getLimit(ss, "open files", "", RLIMIT_NOFILE, 0);
  getLimit(ss, "file size", "blks", RLIMIT_FSIZE, 0);
#if !defined(__APPLE__)
  getLimit(ss, "pending signals", "", RLIMIT_SIGPENDING, 0);
  getLimit(ss, "file locks", "", RLIMIT_LOCKS, 0);
#endif
  getLimit(ss, "max locked memory", "kb", RLIMIT_MEMLOCK, 10);
  getLimit(ss, "max memory size", "kb", RLIMIT_AS, 10);
  getLimit(ss, "stack size", "kb", RLIMIT_STACK, 10);
  getLimit(ss, "cpu time", "secs", RLIMIT_CPU, 0);
  getLimit(ss, "max user processes", "", RLIMIT_NPROC, 0);

  return ss.str();
}

}  // namespace yb
