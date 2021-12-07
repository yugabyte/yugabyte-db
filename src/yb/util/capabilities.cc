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

#include "yb/util/capabilities.h"


#include "yb/gutil/spinlock.h"

namespace yb {

namespace {

std::vector<CapabilityId>* capabilities_ = nullptr;

class CapabilitiesShutdown {
 public:
  ~CapabilitiesShutdown() {
    delete capabilities_;
  }
};

CapabilitiesShutdown capabilities_shutdown_;

// If capability non zero - add it to capabilities.
// If out is not nullptr - copy all capabilities to it.
void CapabilityHelper(CapabilityId capability, std::vector<CapabilityId>* out) {
  static base::SpinLock mutex(base::SpinLock::LINKER_INITIALIZED);
  base::SpinLockHolder lock(&mutex);
  if (!capabilities_) {
    capabilities_ = new std::vector<CapabilityId>();
  }
  if (capability) {
    capabilities_->push_back(capability);
  }
  if (out) {
    *out = *capabilities_;
  }
}

} // namespace

CapabilityRegisterer::CapabilityRegisterer(CapabilityId capability) {
  CapabilityHelper(capability, nullptr /* out */);
}

std::vector<CapabilityId> Capabilities() {
  std::vector<CapabilityId> result;
  CapabilityHelper(0 /* capability */, &result);
  return result;
}

} // namespace yb
