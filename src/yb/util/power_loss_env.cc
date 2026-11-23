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

#include "yb/util/power_loss_env.h"

#include <unistd.h>

#include <atomic>

#include "yb/util/atomic.h"
#include "yb/util/errno.h"
#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

namespace yb {

struct PowerLossEnv::FileState {
  explicit FileState(uint64_t size) : written_size(size), synced_size(size) {}

  std::atomic<uint64_t> written_size;
  std::atomic<uint64_t> synced_size;
};

class PowerLossEnv::File : public WritableFileWrapper {
 public:
  File(std::unique_ptr<WritableFile> target, FileStatePtr state, bool sync_on_close)
      : WritableFileWrapper(std::move(target)), state_(std::move(state)),
        sync_on_close_(sync_on_close) {}

  Status Append(const Slice& data) override {
    RETURN_NOT_OK(WritableFileWrapper::Append(data));
    state_->written_size += data.size();
    return Status::OK();
  }

  Status AppendSlices(const Slice* slices, size_t num) override {
    RETURN_NOT_OK(WritableFileWrapper::AppendSlices(slices, num));
    for (size_t i = 0; i != num; ++i) {
      state_->written_size += slices[i].size();
    }
    return Status::OK();
  }

  Status Sync() override {
    return SyncAndTrack([this] { return WritableFileWrapper::Sync(); });
  }

  Status Fsync() override {
    return SyncAndTrack([this] { return WritableFileWrapper::Fsync(); });
  }

  Status Close() override {
    if (sync_on_close_) {
      return SyncAndTrack([this] { return WritableFileWrapper::Close(); });
    }
    return WritableFileWrapper::Close();
  }

 private:
  template <class F>
  Status SyncAndTrack(const F& sync) {
    // Only bytes appended before the sync started are guaranteed to be durable.
    auto size = state_->written_size.load();
    RETURN_NOT_OK(sync());
    UpdateAtomicMax(&state_->synced_size, size);
    return Status::OK();
  }

  const FileStatePtr state_;
  const bool sync_on_close_;
};

// WAL segments are written through WritableFile, so RWFile only has to track syncs.
class PowerLossEnv::SyncTrackingRWFile : public RWFile {
 public:
  SyncTrackingRWFile(std::unique_ptr<RWFile> target, FileStatePtr state)
      : target_(std::move(target)), state_(std::move(state)) {}

  Status Read(uint64_t offset, size_t length, Slice* result, uint8_t* scratch) const override {
    return target_->Read(offset, length, result, scratch);
  }

  Status Write(uint64_t offset, const Slice& data) override {
    return target_->Write(offset, data);
  }

  Status PreAllocate(uint64_t offset, size_t length) override {
    return target_->PreAllocate(offset, length);
  }

  Status PunchHole(uint64_t offset, size_t length) override {
    return target_->PunchHole(offset, length);
  }

  Status Flush(FlushMode mode, uint64_t offset, size_t length) override {
    return target_->Flush(mode, offset, length);
  }

  Status Sync() override {
    auto size = state_->written_size.load();
    RETURN_NOT_OK(target_->Sync());
    UpdateAtomicMax(&state_->synced_size, size);
    return Status::OK();
  }

  Status Close() override {
    return target_->Close();
  }

  Result<uint64_t> Size() const override {
    return target_->Size();
  }

  const std::string& filename() const override {
    return target_->filename();
  }

 private:
  const std::unique_ptr<RWFile> target_;
  const FileStatePtr state_;
};

PowerLossEnv::PowerLossEnv() : EnvWrapper(Env::Default()) {}

PowerLossEnv::~PowerLossEnv() = default;

Status PowerLossEnv::NewWritableFile(
    const std::string& fname, std::unique_ptr<WritableFile>* result) {
  return NewWritableFile(WritableFileOptions(), fname, result);
}

Status PowerLossEnv::NewWritableFile(
    const WritableFileOptions& opts, const std::string& fname,
    std::unique_ptr<WritableFile>* result) {
  RETURN_NOT_OK(EnvWrapper::NewWritableFile(opts, fname, result));
  Track(opts, fname, result);
  return Status::OK();
}

Status PowerLossEnv::NewTempWritableFile(
    const WritableFileOptions& opts, const std::string& name_template,
    std::string* created_filename, std::unique_ptr<WritableFile>* result) {
  RETURN_NOT_OK(EnvWrapper::NewTempWritableFile(opts, name_template, created_filename, result));
  Track(opts, *created_filename, result);
  return Status::OK();
}

Status PowerLossEnv::NewRWFile(
    const RWFileOptions& opts, const std::string& fname, std::unique_ptr<RWFile>* result) {
  RETURN_NOT_OK(EnvWrapper::NewRWFile(opts, fname, result));
  std::lock_guard lock(mutex_);
  auto it = files_.find(fname);
  if (it != files_.end()) {
    auto target = std::move(*result);
    *result = std::make_unique<SyncTrackingRWFile>(std::move(target), it->second);
  }
  return Status::OK();
}

Status PowerLossEnv::RenameFile(const std::string& src, const std::string& target) {
  RETURN_NOT_OK(EnvWrapper::RenameFile(src, target));
  std::lock_guard lock(mutex_);
  auto it = files_.find(src);
  if (it != files_.end()) {
    auto state = std::move(it->second);
    files_.erase(it);
    files_[target] = std::move(state);
  }
  return Status::OK();
}

size_t PowerLossEnv::NumTrackedFiles(const PathFilter& filter) {
  size_t result = 0;
  std::lock_guard lock(mutex_);
  for (const auto& [path, state] : files_) {
    if (filter(path) && FileExists(path)) {
      ++result;
    }
  }
  return result;
}

uint64_t PowerLossEnv::UnsyncedBytes(const PathFilter& filter) {
  uint64_t result = 0;
  std::lock_guard lock(mutex_);
  for (const auto& [path, state] : files_) {
    if (filter(path)) {
      result += state->written_size.load() - state->synced_size.load();
    }
  }
  return result;
}

Status PowerLossEnv::DropUnsyncedData(const PathFilter& filter) {
  std::lock_guard lock(mutex_);
  for (const auto& [path, state] : files_) {
    if (!filter(path) || !FileExists(path)) {
      continue;
    }
    auto synced_size = state->synced_size.load();
    auto size = VERIFY_RESULT(GetFileSize(path));
    if (size <= synced_size) {
      continue;
    }
    LOG(INFO) << "Truncating " << path << " from " << size << " to " << synced_size;
    RETURN_NOT_OK(STATUS_FROM_ERRNO_IF_NONZERO_RV(path, truncate(path.c_str(), synced_size)));
  }
  return Status::OK();
}

void PowerLossEnv::Track(
    const WritableFileOptions& opts, const std::string& fname,
    std::unique_ptr<WritableFile>* file) {
  auto state = std::make_shared<FileState>((*file)->Size());
  auto target = std::move(*file);
  *file = std::make_unique<File>(std::move(target), state, opts.sync_on_close);
  std::lock_guard lock(mutex_);
  files_[fname] = std::move(state);
}

} // namespace yb
