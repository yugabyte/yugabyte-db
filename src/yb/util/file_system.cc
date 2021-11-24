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

Status SequentialFileWrapper::Skip(uint64_t n) {
  return target_->Skip(n);
}

Status RandomAccessFileWrapper::Read(
    uint64_t offset, size_t n, Slice* result, uint8_t* scratch) const {
  return target_->Read(offset, n , result, scratch);
}

Result<uint64_t> RandomAccessFileWrapper::Size() const {
  return target_->Size();
}

Result<uint64_t> RandomAccessFileWrapper::INode() const {
  return target_->INode();
}

Status RandomAccessFileWrapper::InvalidateCache(size_t offset, size_t length) {
  return target_->InvalidateCache(offset, length);
}

} // namespace yb
