// Copyright (c) YugabyteDB, Inc.
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

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "yb/gutil/thread_annotations.h"

#include "yb/util/env.h"

namespace yb {

// Tracks how much of each written file was fsynced, so files can be cut back to that size as a
// power loss would.
class PowerLossEnv : public EnvWrapper {
 public:
  using PathFilter = std::function<bool(const std::string&)>;

  PowerLossEnv();
  ~PowerLossEnv();

  Status NewWritableFile(const std::string& fname, std::unique_ptr<WritableFile>* result) override;

  Status NewWritableFile(
      const WritableFileOptions& opts, const std::string& fname,
      std::unique_ptr<WritableFile>* result) override;

  Status NewTempWritableFile(
      const WritableFileOptions& opts, const std::string& name_template,
      std::string* created_filename, std::unique_ptr<WritableFile>* result) override;

  // Bootstrap fsyncs an unclosed WAL segment it does not reuse through RWFile.
  Status NewRWFile(
      const RWFileOptions& opts, const std::string& fname,
      std::unique_ptr<RWFile>* result) override;

  // Files could be written under a temporary name and renamed once complete.
  Status RenameFile(const std::string& src, const std::string& target) override;

  // Returns the number of existing tracked files accepted by filter.
  size_t NumTrackedFiles(const PathFilter& filter);

  // Returns the number of bytes appended to files accepted by filter after their last fsync.
  uint64_t UnsyncedBytes(const PathFilter& filter);

  // Truncates files accepted by filter to the size they had at their last fsync.
  Status DropUnsyncedData(const PathFilter& filter);

 private:
  struct FileState;
  using FileStatePtr = std::shared_ptr<FileState>;
  class File;
  class SyncTrackingRWFile;

  void Track(
      const WritableFileOptions& opts, const std::string& fname,
      std::unique_ptr<WritableFile>* file);

  std::mutex mutex_;
  std::map<std::string, FileStatePtr> files_ GUARDED_BY(mutex_);
};

} // namespace yb
