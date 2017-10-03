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
#ifndef KUDU_CFILE_BLOCK_POINTER_H
#define KUDU_CFILE_BLOCK_POINTER_H

#include <stdio.h>
#include <string>

#include "kudu/cfile/cfile.pb.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/coding-inl.h"
#include "kudu/util/coding.h"
#include "kudu/util/status.h"

namespace kudu { namespace cfile {

using std::string;

class BlockPointer {
 public:
  BlockPointer() {}
  BlockPointer(const BlockPointer &from) :
    offset_(from.offset_),
    size_(from.size_) {}

  explicit BlockPointer(const BlockPointerPB &from) :
    offset_(from.offset()),
    size_(from.size()) {
  }

  BlockPointer(uint64_t offset, uint64_t size) :
    offset_(offset),
    size_(size) {}

  string ToString() const {
    return strings::Substitute("offset=$0 size=$1", offset_, size_);
  }

  template<class StrType>
  void EncodeTo(StrType *s) const {
    PutVarint64(s, offset_);
    InlinePutVarint32(s, size_);
  }

  Status DecodeFrom(const uint8_t *data, const uint8_t *limit) {
    data = GetVarint64Ptr(data, limit, &offset_);
    if (!data) {
      return Status::Corruption("bad block pointer");
    }

    data = GetVarint32Ptr(data, limit, &size_);
    if (!data) {
      return Status::Corruption("bad block pointer");
    }

    return Status::OK();
  }

  void CopyToPB(BlockPointerPB *pb) const {
    pb->set_offset(offset_);
    pb->set_size(size_);
  }

  uint64_t offset() const {
    return offset_;
  }

  uint32_t size() const {
    return size_;
  }

 private:
  uint64_t offset_;
  uint32_t size_;
};


} // namespace cfile
} // namespace kudu
#endif
