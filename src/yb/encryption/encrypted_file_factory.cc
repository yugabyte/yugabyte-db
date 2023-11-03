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

#include "yb/encryption/encrypted_file_factory.h"

#include "yb/encryption/cipher_stream.h"
#include "yb/encryption/encrypted_file.h"
#include "yb/encryption/encryption_util.h"
#include "yb/encryption/header_manager.h"

namespace yb {
namespace encryption {

// An encrypted file implementation for a writable file.
class EncryptedWritableFile : public WritableFileWrapper {
 public:

  static Status Create(std::unique_ptr<WritableFile>* result,
                       HeaderManager* header_manager,
                       std::unique_ptr<WritableFile> underlying) {
    return CreateWritableFile<EncryptedWritableFile>(
        result, header_manager, std::move(underlying));
  }

  // Default constructor.
  EncryptedWritableFile(std::unique_ptr<WritableFile> file,
                        std::unique_ptr<encryption::BlockAccessCipherStream> stream,
                        uint32_t header_size)
      : WritableFileWrapper(std::move(file)), stream_(std::move(stream)), header_size_(header_size)
  {}

  ~EncryptedWritableFile() {}

  Status Append(const Slice& data) override {
    if (data.empty()) {
      return Status::OK();
    }

    uint8_t* buf = static_cast<uint8_t*>(EncryptionBuffer::Get()->GetBuffer(data.size()));
    RETURN_NOT_OK(stream_->Encrypt(WritableFileWrapper::Size() - header_size_, data, buf));
    return WritableFileWrapper::Append(Slice(buf, data.size()));
  }

  Status AppendSlices(const Slice* slices, size_t num) override {
    auto end = slices + num;
    size_t total_size = 0;
    for (auto it = slices; it != end; ++it) {
      total_size += it->size();
    }
    if (total_size == 0) {
      return Status::OK();
    }

    uint8_t* buf = static_cast<uint8_t*>(EncryptionBuffer::Get()->GetBuffer(total_size));
    auto write_pos = buf;
    auto offset = WritableFileWrapper::Size() - header_size_;
    for (auto it = slices; it != end; ++it) {
      RETURN_NOT_OK(stream_->Encrypt(offset, *it, write_pos));
      write_pos += it->size();
      offset += it->size();
    }

    return WritableFileWrapper::Append(Slice(buf, total_size));
  }

  uint64_t Size() const override {
    return WritableFileWrapper::Size() - header_size_;
  }

 private:
  std::unique_ptr<BlockAccessCipherStream> stream_;
  uint32_t header_size_;
};

// EncryptedFileFactory creates encrypted files.
class EncryptedFileFactory : public FileFactoryWrapper {
 public:
  explicit EncryptedFileFactory(FileFactory* file_factory,
                                std::unique_ptr<HeaderManager> header_manager) :
  FileFactoryWrapper(file_factory),
  header_manager_(std::move(header_manager)) {
    LOG(INFO) << "Created encrypted file factory";
  }

  ~EncryptedFileFactory() {}

  // NewRandomAccessFile opens a file for random read access.
  Status NewRandomAccessFile(const std::string& fname,
                             std::unique_ptr<yb::RandomAccessFile>* result) override {
    std::unique_ptr<yb::RandomAccessFile> underlying;
    RETURN_NOT_OK(FileFactoryWrapper::NewRandomAccessFile(fname, &underlying));
    return EncryptedRandomAccessFile::Create(result, header_manager_.get(), std::move(underlying));
  }

  Status NewTempWritableFile(const WritableFileOptions& opts,
                             const std::string& name_template,
                             std::string* created_filename,
                             std::unique_ptr<WritableFile>* result) override {
    std::unique_ptr<WritableFile> underlying;
    RETURN_NOT_OK(FileFactoryWrapper::NewTempWritableFile(
        opts, name_template, created_filename, &underlying));
    return EncryptedWritableFile::Create(result, header_manager_.get(), std::move(underlying));
  }

  Status NewWritableFile(const WritableFileOptions& opts,
                         const std::string& fname,
                         std::unique_ptr<WritableFile>* result) override {
    // Currently, only WAL reuse feature is using the NewWritableFile encryption.
    if (opts.initial_offset.has_value() &&
        opts.mode == EnvWrapper::OPEN_EXISTING) {
      std::unique_ptr<yb::RandomAccessFile> underlying_r;
      RETURN_NOT_OK(FileFactoryWrapper::NewRandomAccessFile(fname, &underlying_r));
      std::unique_ptr<BlockAccessCipherStream> stream;
      uint32_t header_size;
      std::string universe_key_id;
      const auto file_encrypted = VERIFY_RESULT(GetEncryptionInfoFromFile<uint8_t>(
          header_manager_.get(), underlying_r.get(), &stream, &header_size, &universe_key_id));
      if (file_encrypted != VERIFY_RESULT(header_manager_->IsEncryptionEnabled())) {
        return STATUS(NotSupported,
            "File's encryption state doesn't match with tserver's encryption state");
      }
      if (file_encrypted) {
        if (universe_key_id != VERIFY_RESULT(header_manager_->GetLatestUniverseKeyId())) {
          return STATUS(NotSupported, "File's universe key id is not the latest universe key id");
        }
        auto new_opts = opts;
        new_opts.initial_offset = opts.initial_offset.value() + header_size;
        std::unique_ptr<WritableFile> underlying;
        RETURN_NOT_OK(FileFactoryWrapper::NewWritableFile(new_opts, fname, &underlying));
        result->reset(new EncryptedWritableFile(
            std::move(underlying), std::move(stream), header_size));
        return Status::OK();
      }
    }
    return FileFactoryWrapper::NewWritableFile(opts, fname, result);
  }

  bool IsEncrypted() const override {
    return true;
  }

 private:
  std::unique_ptr<HeaderManager> header_manager_;
};

std::unique_ptr<yb::Env> NewEncryptedEnv(std::unique_ptr<HeaderManager> header_manager) {
  auto file_factory = Env::DefaultFileFactory();
  auto encrypted_file_factory = std::make_unique<EncryptedFileFactory>(file_factory,
                                                                       std::move(header_manager));
  auto encrypted_env = Env::NewDefaultEnv(std::move(encrypted_file_factory));
  return encrypted_env;
}

} // namespace encryption
} // namespace yb
