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

#ifndef YB_ENCRYPTION_ENCRYPTION_TEST_UTIL_H
#define YB_ENCRYPTION_ENCRYPTION_TEST_UTIL_H

#include "yb/encryption/encryption_util.h"

#include "yb/util/slice.h"
#include "yb/util/test_util.h"

namespace yb {
namespace encryption {

void DoTest(std::function<void(uint32_t, uint32_t)> file_op, int32_t size);

template <typename Writable>
void TestWrites(Writable* file, const Slice& data) {
  DoTest([&](uint32_t begin, uint32_t end) {
    ASSERT_OK(file->Append(Slice(data.data() + begin, end - begin)));
  }, data.size());
}

template <typename BufType, typename Readable>
void TestRandomAccessReads(Readable* file, const Slice& data) {
  auto buf = static_cast<BufType*>(EncryptionBuffer::Get()->GetBuffer(data.size()));
  DoTest([&](uint32_t begin, uint32_t end) {
    Slice result;
    ASSERT_OK(file->Read(begin, end - begin, &result, buf));
    ASSERT_EQ(result.ToDebugHexString(),
              Slice(data.data() + begin, end - begin).ToDebugHexString());
  }, data.size());
}

template <typename BufType, typename Readable>
void TestSequentialReads(Readable* file, const Slice& data) {
  DoTest([&](uint32_t begin, uint32_t end) {
    auto buf = static_cast<BufType*>(EncryptionBuffer::Get()->GetBuffer(data.size()));
    Slice result;
    ASSERT_OK(file->Read(end - begin, &result, buf));
    ASSERT_EQ(result.ToDebugHexString(),
              Slice(data.data() + begin, end - begin).ToDebugHexString());
  }, data.size());
}

} // namespace encryption
} // namespace yb

#endif // YB_ENCRYPTION_ENCRYPTION_TEST_UTIL_H
