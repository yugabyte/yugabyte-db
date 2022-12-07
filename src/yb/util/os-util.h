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
// - Namespace + imports.
// - Fixes for cpplint.
// - Fixed parsing when thread names have spaces.

#pragma once

#include <string>

#include "yb/util/status_fwd.h"

namespace yb {

// Utility methods to read interesting values from /proc.
// TODO: Get stats for parent process.

// Container struct for statistics read from the /proc filesystem for a thread.
struct ThreadStats {
  int64_t user_ns;
  int64_t kernel_ns;
  int64_t iowait_ns;

  // Default constructor zeroes all members in case structure can't be filled by
  // GetThreadStats.
  ThreadStats() : user_ns(0), kernel_ns(0), iowait_ns(0) { }
};

// Populates ThreadStats object using a given buffer. The buffer is expected to
// conform to /proc/<pid>/task/<tid>/stat layout; an error will be returned otherwise.
//
// If 'name' is supplied, the extracted thread name will be written to it.
Status ParseStat(const std::string&buffer, std::string* name, ThreadStats* stats);

// Populates ThreadStats object for a given thread by reading from
// /proc/<pid>/task/<tid>/stat. Returns OK unless the file cannot be read or is in an
// unrecognised format, or if the kernel version is not modern enough.
Status GetThreadStats(int64_t tid, ThreadStats* stats);

// Runs a shell command. Returns false if there was any error (either failure to launch or
// non-0 exit code), and true otherwise. *msg is set to an error message including the OS
// error string, if any, and the first 1k of output if there was any error, or just the
// first 1k of output otherwise.
bool RunShellProcess(const std::string& cmd, std::string* msg);

} // namespace yb
