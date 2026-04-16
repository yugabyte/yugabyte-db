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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <zlib.h>

#include "yb/gutil/casts.h"

#include "yb/rocksdb/util/xxhash.h"

#include "yb/util/crc.h"
#include "yb/util/status.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_util.h"

using std::string;

namespace yb {
namespace crc {

using strings::Substitute;

class CrcTest : public YBTest {
 protected:

  // Returns pointer to data which must be deleted by caller.
  static void GenerateBenchmarkData(const uint8_t** bufptr, size_t* buflen) {
    const uint32_t kNumNumbers = 1000000;
    const uint32_t kBytesPerNumber = sizeof(uint32_t);
    const uint32_t kLength = kNumNumbers * kBytesPerNumber;
    auto buf = new uint8_t[kLength];
    for (uint32_t i = 0; i < kNumNumbers; i++) {
      memcpy(buf + (i * kBytesPerNumber), &i, kBytesPerNumber);
    }
    *bufptr = buf;
    *buflen = kLength;
  }

};

// Basic functionality test.
TEST_F(CrcTest, TestCRC32C) {
  const string test_data("abcdefgh");
  Crc* crc32c = GetCrc32cInstance();
  uint64_t data_crc = 0;
  crc32c->Compute(test_data.data(), test_data.length(), &data_crc);
  char buf[kFastToBufferSize];
  const char* output = FastHex64ToBuffer(data_crc, buf);
  LOG(INFO) << "CRC32C of " << test_data << " is: 0x" << output << " (full 64 bits)";
  output = FastHex32ToBuffer(static_cast<uint32_t>(data_crc), buf);
  LOG(INFO) << "CRC32C of " << test_data << " is: 0x" << output << " (truncated 32 bits)";
  ASSERT_EQ(0xa9421b7, data_crc); // Known value from crcutil usage test program.
}

template <class F>
void MeasureTime(const std::string_view& name, Slice buffer, const F& f) {
  int kNumRuns = 1000;
  if (AllowSlowTests()) {
    kNumRuns = 40000;
  }
  const uint64_t kNumBytes = kNumRuns * buffer.size();
  Stopwatch sw;
  sw.start();
  for (int i = 0; i < kNumRuns; i++) {
    f(buffer);
  }
  sw.stop();
  CpuTimes elapsed = sw.elapsed();
  LOG(INFO) << Format("$0 runs of $6 on $1 bytes of data (total: $2 bytes)"
                      " in $3 seconds; $4 bytes per millisecond, $5 bytes per nanosecond!",
                      kNumRuns, buffer.size(), kNumBytes, elapsed.wall_seconds(),
                      (kNumBytes / elapsed.wall_millis()),
                      (kNumBytes / elapsed.wall),
                      name);
}

// Simple benchmark of CRC32C throughput.
// We should expect about 8 bytes per cycle in throughput on a single core.
TEST_F(CrcTest, BenchmarkCRC32C) {
  std::unique_ptr<const uint8_t[]> data;
  const uint8_t* buf;
  size_t buflen;
  GenerateBenchmarkData(&buf, &buflen);
  data.reset(buf);
  Slice slice(buf, buflen);
  MeasureTime("CRC32C", slice, [crc32c = GetCrc32cInstance()](Slice buffer) {
    uint64_t cksum;
    crc32c->Compute(buffer.data(), buffer.size(), &cksum);
  });
  MeasureTime("Adler32", slice, [](Slice buffer) {
    adler32_z(0, buffer.data(), buffer.size());
  });
  MeasureTime("xxHash", slice, [](Slice buffer) {
    rocksdb::XXH32(buffer.data(), narrow_cast<int>(buffer.size()), 0);
  });
}

} // namespace crc
} // namespace yb
