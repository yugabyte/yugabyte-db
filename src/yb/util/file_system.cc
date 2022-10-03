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

#include "yb/util/file_system.h"

#include "yb/util/result.h"

namespace yb {

const FileSystemOptions FileSystemOptions::kDefault;

Status SequentialFile::InvalidateCache(size_t offset, size_t length) {
  return STATUS(NotSupported, "InvalidateCache not supported.");
}

Status RandomAccessFile::ReadAndValidate(
    uint64_t offset, size_t n, Slice* result, char* scratch, const ReadValidator& validator) {
  RETURN_NOT_OK(Read(offset, n, result, scratch));
  return validator.Validate(*result);
}

Status RandomAccessFile::Read(uint64_t offset, size_t n, Slice* result, char* scratch) {
  return Read(offset, n, result, reinterpret_cast<uint8_t*>(scratch));
}

Status RandomAccessFile::InvalidateCache(size_t offset, size_t length) {
  return STATUS(NotSupported, "InvalidateCache not supported.");
}

Status SequentialFileWrapper::InvalidateCache(size_t offset, size_t length) {
  return target_->InvalidateCache(offset, length);
}

Status SequentialFileWrapper::Read(size_t n, Slice* result, uint8_t* scratch) {
  return target_->Read(n, result, scratch);
}

Status SequentialFileWrapper::Skip(uint64_t n) { return target_->Skip(n); }

Status RandomAccessFileWrapper::Read(
    uint64_t offset, size_t n, Slice* result, uint8_t* scratch) const {
  return target_->Read(offset, n, result, scratch);
}

Result<uint64_t> RandomAccessFileWrapper::Size() const { return target_->Size(); }

Result<uint64_t> RandomAccessFileWrapper::INode() const { return target_->INode(); }

Status RandomAccessFileWrapper::InvalidateCache(size_t offset, size_t length) {
  return target_->InvalidateCache(offset, length);
}

} // namespace yb

namespace rocksdb {

WritableFile::~WritableFile() {
}

void WritableFile::PrepareWrite(size_t offset, size_t len) {
  if (preallocation_block_size_ == 0) {
    return;
  }
  // If this write would cross one or more preallocation blocks,
  // determine what the last preallocation block necesessary to
  // cover this write would be and Allocate to that point.
  const auto block_size = preallocation_block_size_;
  size_t new_last_preallocated_block =
    (offset + len + block_size - 1) / block_size;
  if (new_last_preallocated_block > last_preallocated_block_) {
    size_t num_spanned_blocks =
      new_last_preallocated_block - last_preallocated_block_;
    WARN_NOT_OK(
        Allocate(block_size * last_preallocated_block_, block_size * num_spanned_blocks),
        "Failed to pre-allocate space for a file");
    last_preallocated_block_ = new_last_preallocated_block;
  }
}

Status WritableFile::PositionedAppend(const Slice& /* data */, uint64_t /* offset */) {
  return STATUS(NotSupported, "PositionedAppend not supported");
}

// Truncate is necessary to trim the file to the correct size
// before closing. It is not always possible to keep track of the file
// size due to whole pages writes. The behavior is undefined if called
// with other writes to follow.
Status WritableFile::Truncate(uint64_t size) {
  return Status::OK();
}

Status WritableFile::RangeSync(uint64_t offset, uint64_t nbytes) {
  return Status::OK();
}

Status WritableFile::Fsync() {
  return Sync();
}

Status WritableFile::InvalidateCache(size_t offset, size_t length) {
  return STATUS(NotSupported, "InvalidateCache not supported.");
}

Status WritableFile::Allocate(uint64_t offset, uint64_t len) {
  return Status::OK();
}

Status WritableFileWrapper::Append(const Slice& data) {
  return target_->Append(data);
}

Status WritableFileWrapper::PositionedAppend(const Slice& data, uint64_t offset) {
  return target_->PositionedAppend(data, offset);
}

Status WritableFileWrapper::Truncate(uint64_t size) {
  return target_->Truncate(size);
}

Status WritableFileWrapper::Close() {
  return target_->Close();
}

Status WritableFileWrapper::Flush() {
  return target_->Flush();
}

Status WritableFileWrapper::Sync() {
  return target_->Sync();
}

Status WritableFileWrapper::Fsync() {
  return target_->Fsync();
}

Status WritableFileWrapper::InvalidateCache(size_t offset, size_t length) {
  return target_->InvalidateCache(offset, length);
}

Status WritableFileWrapper::Allocate(uint64_t offset, uint64_t len) {
  return target_->Allocate(offset, len);
}

Status WritableFileWrapper::RangeSync(uint64_t offset, uint64_t nbytes) {
  return target_->RangeSync(offset, nbytes);
}

} // namespace rocksdb
