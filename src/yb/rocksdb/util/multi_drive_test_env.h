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

#include <string>

#include "yb/rocksdb/env.h"

#include "yb/util/status.h"
#include "yb/util/multi_drive_test_env.h"

namespace rocksdb {

class MultiDriveTestEnv : public EnvWrapper, public yb::MultiDriveTestEnvBase {
 public:
  MultiDriveTestEnv() : EnvWrapper(Env::Default()) {}

  Status NewSequentialFile(const std::string& f,
                           std::unique_ptr<SequentialFile>* r,
                           const EnvOptions& options) override {
    RETURN_NOT_OK(FailureStatus(f));
    return target()->NewSequentialFile(f, r, options);
  }

  Status NewRandomAccessFile(const std::string& f,
                             std::unique_ptr<RandomAccessFile>* r,
                             const EnvOptions& options) override {
    RETURN_NOT_OK(FailureStatus(f));
    return target()->NewRandomAccessFile(f, r, options);
  }

  Status NewWritableFile(const std::string& f,
                         std::unique_ptr<WritableFile>* r,
                         const EnvOptions& options) override {
    RETURN_NOT_OK(FailureStatus(f));
    return target()->NewWritableFile(f, r, options);
  }

  Status ReuseWritableFile(const std::string& f,
                           const std::string& old_fname,
                           std::unique_ptr<WritableFile>* r,
                           const EnvOptions& options) override {
    RETURN_NOT_OK(FailureStatus(f));
    return target()->ReuseWritableFile(f, old_fname, r, options);
  }
};

} // namespace rocksdb
