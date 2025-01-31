// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
#pragma once

#include <memory>
#include <string>

#include "yb/util/status.h"

namespace yb::tablet {

class RaftGroupMetadata;

} // namespace yb::tablet

namespace yb::tserver {

class TSTabletManager;

class TabletMetadataValidator final {
  class Impl;

 public:
  ~TabletMetadataValidator();
  explicit TabletMetadataValidator(const std::string& log_prefix, TSTabletManager* tablet_manager);

  Status Init();
  void StartShutdown();
  void CompleteShutdown();

  // Checks if a tablet is a candidate for the validation.
  void ScheduleValidation(const tablet::RaftGroupMetadata& metadata);

 private:
  std::unique_ptr<Impl> impl_;
};

} // namespace yb::tserver
