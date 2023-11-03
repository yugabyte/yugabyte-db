//
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
//

#pragma once

#include <shared_mutex>
#include <set>
#include <string>

#include <boost/algorithm/string/predicate.hpp>

#include "yb/gutil/thread_annotations.h"

#include "yb/util/env.h"
#include "yb/util/status.h"

#include "yb/util/monotime.h"

namespace yb {

class MultiDriveTestEnvBase {
 public:
  void AddFailedPath(const std::string& path);

 protected:
  Status FailureStatus(const std::string& filename) const;

  std::set<std::string> failed_set_;
  mutable std::shared_mutex data_mutex_;
};

class MultiDriveTestEnv : public EnvWrapper, public MultiDriveTestEnvBase {
 public:
  MultiDriveTestEnv() : EnvWrapper(Env::Default()) {}

  Status NewSequentialFile(const std::string& f,
                           std::unique_ptr<SequentialFile>* r) override;
  Status NewRandomAccessFile(const std::string& f,
                             std::unique_ptr<RandomAccessFile>* r) override;
  Status NewWritableFile(const std::string& f, std::unique_ptr<WritableFile>* r) override;
  Status NewWritableFile(const WritableFileOptions& o,
                         const std::string& f,
                         std::unique_ptr<WritableFile>* r) override;
  Status NewTempWritableFile(const WritableFileOptions& o, const std::string& t,
                             std::string* f, std::unique_ptr<WritableFile>* r) override;
  Status NewRWFile(const std::string& f, std::unique_ptr<RWFile>* r) override;
  Status NewRWFile(const RWFileOptions& o,
                   const std::string& f,
                   std::unique_ptr<RWFile>* r) override;
};

} // namespace yb
