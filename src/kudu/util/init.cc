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

#include "kudu/util/init.h"

#include <string>

#include "kudu/gutil/cpu.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/status.h"

using std::string;

namespace kudu {

Status BadCPUStatus(const base::CPU& cpu, const char* instruction_set) {
  return Status::NotSupported(strings::Substitute(
      "The CPU on this system ($0) does not support the $1 instruction "
      "set which is required for running Kudu.",
      cpu.cpu_brand(), instruction_set));
}

Status CheckCPUFlags() {
  base::CPU cpu;
  if (!cpu.has_sse42()) {
    return BadCPUStatus(cpu, "SSE4.2");
  }

  if (!cpu.has_ssse3()) {
    return BadCPUStatus(cpu, "SSSE3");
  }

  return Status::OK();
}

void InitKuduOrDie() {
  CHECK_OK(CheckCPUFlags());
}

} // namespace kudu
