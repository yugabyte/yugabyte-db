// Copyright 2022 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

// Copied from absl/base/internal/cpu_detect.h (Abseil master 921be849a0 of 2026-11-05), which is
// newer than the prebuilt Abseil library, with the namespace changed so it does not clash with it.

#pragma once

#include "absl/base/config.h"

namespace yb::crc::internal {

// Enumeration of architectures that we have special-case tuning parameters for.
// This set may change over time.
enum class CpuType {
  kUnknown,
  kIntelHaswell,
  kAmdRome,
  kAmdNaples,
  kAmdMilan,
  kAmdGenoa,
  kAmdSiena,
  kAmdTurin,
  kAmdVenice,
  kAmdRyzenV3000,
  kIntelCascadelakeXeon,
  kIntelSkylakeXeon,
  kIntelBroadwell,
  kIntelIcelake,
  kIntelSapphirerapids,
  kIntelEmeraldrapids,
  kIntelGraniterapids,
  kIntelSkylake,
  kIntelIvybridge,
  kIntelSandybridge,
  kIntelWestmere,
  kArmNeoverseN1,
  kArmNeoverseV1,
  kAmpereSiryn,
  kArmNeoverseN2,
  kArmNeoverseV2,
  kArmNeoverseV3,
  kArmNeoverseN3,
  kArmNeoverseN4,
  kNvidiaGrace,
};

// Returns the type of host CPU this code is running on.  Returns kUnknown if
// the host CPU is of unknown type, or if detection otherwise fails.
CpuType GetCpuType();

// Returns whether the host CPU supports the CPU features needed for our
// accelerated implementations. The engine factory checks it for every
// CpuType, since PMULL is optional on all of them.
bool SupportsArmCRC32PMULL();

// Returns whether the host CPU supports BMI2 instructions.
bool SupportsBmi2();

// Returns whether the host CPU supports simultaneous multithreading (SMT) and
// if it is enabled.
bool IsSMTEnabled();

// Returns how many hardware contexts per CPU exist.
int NumContextsPerCPU();

}  // namespace yb::crc::internal
