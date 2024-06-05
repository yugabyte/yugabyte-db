// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
// Classes used internally by pb_util.h.
// This header should not be included by anything but pb_util and its tests.
#pragma once

#include "yb/util/logging.h"
#include <google/protobuf/io/zero_copy_stream.h>

#include "yb/util/env.h"
#include "yb/util/status.h"

namespace yb {
namespace pb_util {
namespace internal {

// Input Stream used by ParseFromSequentialFile()
class SequentialFileFileInputStream : public google::protobuf::io::ZeroCopyInputStream {
 public:
  explicit SequentialFileFileInputStream(SequentialFile *rfile,
                                         size_t buffer_size = kDefaultBufferSize)
    : buffer_used_(0), buffer_offset_(0),
      buffer_size_(buffer_size), buffer_(new uint8[buffer_size_]),
      total_read_(0), rfile_(rfile) {
    CHECK_GT(buffer_size, 0);
  }

  ~SequentialFileFileInputStream() {
  }

  bool Next(const void **data, int *size) override;
  bool Skip(int count) override;

  void BackUp(int count) override {
    CHECK_GE(count, 0);
    CHECK_LE(count, buffer_offset_);
    buffer_offset_ -= count;
    total_read_ -= count;
  }

  int64 ByteCount() const override {
    return total_read_;
  }

 private:
  static const size_t kDefaultBufferSize = 8192;

  Status status_;

  size_t buffer_used_;
  size_t buffer_offset_;
  const size_t buffer_size_;
  std::unique_ptr<uint8_t[]> buffer_;

  size_t total_read_;
  SequentialFile *rfile_;
};

// Output Stream used by SerializeToWritableFile()
class WritableFileOutputStream : public google::protobuf::io::ZeroCopyOutputStream {
 public:
  explicit WritableFileOutputStream(WritableFile *wfile, size_t buffer_size = kDefaultBufferSize)
    : buffer_offset_(0), buffer_size_(buffer_size), buffer_(new uint8[buffer_size_]),
      flushed_(0), wfile_(wfile) {
    CHECK_GT(buffer_size, 0);
  }

  ~WritableFileOutputStream() {
  }

  bool Flush() {
    if (buffer_offset_ > 0) {
      Slice data(buffer_.get(), buffer_offset_);
      status_ = wfile_->Append(data);
      flushed_ += buffer_offset_;
      buffer_offset_ = 0;
    }
    return status_.ok();
  }

  bool Next(void **data, int *size) override;

  void BackUp(int count) override {
    CHECK_GE(count, 0);
    CHECK_LE(count, buffer_offset_);
    buffer_offset_ -= count;
  }

  int64 ByteCount() const override {
    return flushed_ + buffer_offset_;
  }

 private:
  static const size_t kDefaultBufferSize = 8192;

  Status status_;

  size_t buffer_offset_;
  const size_t buffer_size_;
  std::unique_ptr<uint8_t[]> buffer_;

  size_t flushed_;
  WritableFile *wfile_;
};

} // namespace internal
} // namespace pb_util
} // namespace yb
