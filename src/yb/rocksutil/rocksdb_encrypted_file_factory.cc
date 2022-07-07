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

#include "yb/rocksutil/rocksdb_encrypted_file_factory.h"

#include "yb/encryption/cipher_stream.h"
#include "yb/encryption/encrypted_file.h"
#include "yb/encryption/encryption_util.h"
#include "yb/encryption/header_manager.h"

namespace yb {

// An encrypted file implementation for sequential reads.
class EncryptedSequentialFile : public SequentialFileWrapper {
 public:
  static Status Create(std::unique_ptr<rocksdb::SequentialFile>* result,
                       encryption::HeaderManager* header_manager,
                       std::unique_ptr<rocksdb::SequentialFile> underlying_seq,
                       std::unique_ptr<rocksdb::RandomAccessFile> underlying_ra) {
    result->reset();
    std::unique_ptr<encryption::BlockAccessCipherStream> stream;
    uint32_t header_size;

    const auto file_encrypted = VERIFY_RESULT(encryption::GetEncryptionInfoFromFile<uint8_t>(
        header_manager, underlying_ra.get(), &stream, &header_size));
    if (!file_encrypted) {
      *result = std::move(underlying_seq);
      return Status::OK();
    }

    RETURN_NOT_OK(underlying_seq->Skip(header_size));
    *result = std::make_unique<EncryptedSequentialFile>(
        std::move(underlying_seq), std::move(stream));
    return Status::OK();
  }

  // Default constructor.
  EncryptedSequentialFile(std::unique_ptr<rocksdb::SequentialFile> file,
                          std::unique_ptr<encryption::BlockAccessCipherStream> stream)
      : SequentialFileWrapper(std::move(file)), stream_(std::move(stream)) {}

  ~EncryptedSequentialFile() {}

  Status Read(size_t n, Slice* result, uint8_t* scratch) override {
    if (!scratch) {
      return STATUS(InvalidArgument, "scratch argument is null.");
    }
    uint8_t* buf = static_cast<uint8_t*>(encryption::EncryptionBuffer::Get()->GetBuffer(n));
    RETURN_NOT_OK(SequentialFileWrapper::Read(n, result, buf));
    RETURN_NOT_OK(stream_->Decrypt(offset_, *result, scratch));
    *result = Slice(scratch, result->size());
    offset_ += result->size();
    return Status::OK();
  }

  Status Skip(uint64_t n) override {
    RETURN_NOT_OK(SequentialFileWrapper::Skip(n));
    offset_ += n;
    return Status::OK();
  }

 private:
  std::unique_ptr<encryption::BlockAccessCipherStream> stream_;
  uint64_t offset_ = 0;
};

// An encrypted file implementation for a writable file.
class RocksDBEncryptedWritableFile : public rocksdb::WritableFileWrapper {
 public:
  static Status Create(std::unique_ptr<rocksdb::WritableFile>* result,
                       encryption::HeaderManager* header_manager,
                       std::unique_ptr<rocksdb::WritableFile> underlying) {
    return encryption::CreateWritableFile<RocksDBEncryptedWritableFile>(
        result, header_manager, std::move(underlying));
  }

  // Default constructor.
  RocksDBEncryptedWritableFile(std::unique_ptr<rocksdb::WritableFile> file,
                               std::unique_ptr<encryption::BlockAccessCipherStream> stream,
                               uint32_t header_size)
      : WritableFileWrapper(std::move(file)), stream_(std::move(stream)),
        header_size_(header_size) {}

  ~RocksDBEncryptedWritableFile() {}

  Status Append(const Slice& data) override {
    if (data.size() > 0) {
      char* buf = static_cast<char*>(encryption::EncryptionBuffer::Get()->GetBuffer(data.size()));
      RETURN_NOT_OK(stream_->Encrypt(GetFileSize() - header_size_, data, buf));
      RETURN_NOT_OK(WritableFileWrapper::Append(Slice(buf, data.size())));
    }
    return Status::OK();
  }

 private:
  std::unique_ptr<encryption::BlockAccessCipherStream> stream_;
  uint32_t header_size_;
};

// EncryptedEnv implements an Env wrapper that adds encryption to files stored on disk.
class RocksDBEncryptedFileFactory : public rocksdb::RocksDBFileFactoryWrapper {
 public:
  explicit RocksDBEncryptedFileFactory(
      rocksdb::RocksDBFileFactory* factory,
      std::unique_ptr<encryption::HeaderManager> header_manager) :
      RocksDBFileFactoryWrapper(factory),
      header_manager_(std::move(header_manager)) {
    VLOG(1) << "Created RocksDB encrypted env";
  }

  ~RocksDBEncryptedFileFactory() {}

  // NewRandomAccessFile opens a file for random read access.
  Status NewRandomAccessFile(const std::string& fname,
                             std::unique_ptr<rocksdb::RandomAccessFile>* result,
                             const rocksdb::EnvOptions& options) override {
    std::unique_ptr<rocksdb::RandomAccessFile> underlying;
    RETURN_NOT_OK(RocksDBFileFactoryWrapper::NewRandomAccessFile(fname, &underlying, options));
    return encryption::EncryptedRandomAccessFile::Create(
        result, header_manager_.get(), std::move(underlying));
  }

  Status NewWritableFile(const std::string& fname, std::unique_ptr<rocksdb::WritableFile>* result,
                         const rocksdb::EnvOptions& options) override {
    std::unique_ptr<rocksdb::WritableFile> underlying;
    RETURN_NOT_OK(RocksDBFileFactoryWrapper::NewWritableFile(fname, &underlying, options));
    return RocksDBEncryptedWritableFile::Create(
        result, header_manager_.get(), std::move(underlying));
  }

  Status NewSequentialFile(const std::string& fname,
                           std::unique_ptr<rocksdb::SequentialFile>* result,
                           const rocksdb::EnvOptions& options) override {
    // Open file using underlying Env implementation
    std::unique_ptr<rocksdb::RandomAccessFile> underlying_ra;
    RETURN_NOT_OK(RocksDBFileFactoryWrapper::NewRandomAccessFile(fname, &underlying_ra, options));

    std::unique_ptr<rocksdb::SequentialFile> underlying_seq;
    RETURN_NOT_OK(RocksDBFileFactoryWrapper::NewSequentialFile(fname, &underlying_seq, options));

    return EncryptedSequentialFile::Create(
        result, header_manager_.get(), std::move(underlying_seq), std::move(underlying_ra));
  }

  Status GetFileSize(const std::string& fname, uint64_t* size) override {
    RETURN_NOT_OK(RocksDBFileFactoryWrapper::GetFileSize(fname, size));
    // Determine if file is encrypted.
    std::unique_ptr<rocksdb::SequentialFile> underlying;
    RETURN_NOT_OK(RocksDBFileFactoryWrapper::NewSequentialFile(
        fname, &underlying, rocksdb::EnvOptions()));

    *size -= VERIFY_RESULT(encryption::GetHeaderSize(underlying.get(), header_manager_.get()));
    return Status::OK();
  }

  bool IsPlainText() const override {
    return false;
  }

 private:
  std::unique_ptr<encryption::HeaderManager> header_manager_;
};

std::unique_ptr<rocksdb::Env> NewRocksDBEncryptedEnv(
    std::unique_ptr<encryption::HeaderManager> header_manager) {
  auto file_factory = rocksdb::Env::DefaultFileFactory();
  auto encrypted_file_factory = std::make_unique<RocksDBEncryptedFileFactory>(
      file_factory, std::move(header_manager));
  auto encrypted_env = rocksdb::Env::NewRocksDBDefaultEnv(std::move(encrypted_file_factory));
  return encrypted_env;
}

} // namespace yb
