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

#pragma once

#include "yb/encryption/encryption_util.h"

#include "yb/util/slice.h"
#include "yb/util/test_util.h"

namespace yb {
namespace encryption {

void DoTest(const std::function<void(size_t, size_t)>& file_op, size_t size);

template <typename Writable>
void TestWrites(Writable* file, const Slice& data) {
  DoTest([&](size_t begin, size_t end) {
    ASSERT_OK(file->Append(Slice(data.data() + begin, end - begin)));
  }, data.size());
}

template <typename BufType, typename Readable>
void TestRandomAccessReads(Readable* file, const Slice& data) {
  auto buf = static_cast<BufType*>(EncryptionBuffer::Get()->GetBuffer(data.size()));
  DoTest([&](size_t begin, size_t end) {
    Slice result;
    ASSERT_OK(file->Read(begin, end - begin, &result, buf));
    ASSERT_EQ(result.ToDebugHexString(),
              Slice(data.data() + begin, end - begin).ToDebugHexString());
  }, data.size());
}

template <typename BufType, typename Readable>
void TestSequentialReads(Readable* file, const Slice& data) {
  DoTest([&](size_t begin, size_t end) {
    auto buf = static_cast<BufType*>(EncryptionBuffer::Get()->GetBuffer(data.size()));
    Slice result;
    ASSERT_OK(file->Read(end - begin, &result, buf));
    ASSERT_EQ(result.ToDebugHexString(),
              Slice(data.data() + begin, end - begin).ToDebugHexString());
  }, data.size());
}

} // namespace encryption
} // namespace yb
