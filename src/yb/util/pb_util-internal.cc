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
#include "yb/util/pb_util-internal.h"

#include "yb/gutil/casts.h"

namespace yb {
namespace pb_util {
namespace internal {

////////////////////////////////////////////
// SequentialFileFileInputStream
////////////////////////////////////////////

bool SequentialFileFileInputStream::Next(const void **data, int *size) {
  if (PREDICT_FALSE(!status_.ok())) {
    LOG(WARNING) << "Already failed on a previous read: " << status_.ToString();
    return false;
  }

  size_t available = buffer_used_ - buffer_offset_;
  if (available > 0) {
    *data = buffer_.get() + buffer_offset_;
    *size = narrow_cast<int>(available);
    buffer_offset_ += available;
    total_read_ += available;
    return true;
  }

  Slice result;
  status_ = rfile_->Read(buffer_size_, &result, buffer_.get());
  if (!status_.ok()) {
    LOG(WARNING) << "Read at " << buffer_offset_ << " failed: " << status_.ToString();
    return false;
  }

  if (result.data() != buffer_.get()) {
    memcpy(buffer_.get(), result.data(), result.size());
  }

  buffer_used_ = result.size();
  buffer_offset_ = buffer_used_;
  total_read_ += buffer_used_;
  *data = buffer_.get();
  *size = narrow_cast<int>(buffer_used_);
  return buffer_used_ > 0;
}

bool SequentialFileFileInputStream::Skip(int signed_count) {
  CHECK_GT(signed_count, 0);
  size_t count = signed_count;
  auto avail = buffer_used_ - buffer_offset_;
  if (avail > count) {
    buffer_offset_ += count;
    total_read_ += count;
  } else {
    buffer_used_ = 0;
    buffer_offset_ = 0;
    status_ = rfile_->Skip(count - avail);
    total_read_ += count - avail;
  }
  return status_.ok();
}

////////////////////////////////////////////
// WritableFileOutputStream
////////////////////////////////////////////

bool WritableFileOutputStream::Next(void **data, int *size) {
  if (PREDICT_FALSE(!status_.ok())) {
    LOG(WARNING) << "Already failed on a previous write: " << status_.ToString();
    return false;
  }

  size_t available = buffer_size_ - buffer_offset_;
  if (available > 0) {
    *data = buffer_.get() + buffer_offset_;
    *size = narrow_cast<int>(available);
    buffer_offset_ += available;
    return true;
  }

  if (!Flush()) {
    return false;
  }

  buffer_offset_ = buffer_size_;
  *data = buffer_.get();
  *size = narrow_cast<int>(buffer_size_);
  return true;
}

} // namespace internal
} // namespace pb_util
} // namespace yb
