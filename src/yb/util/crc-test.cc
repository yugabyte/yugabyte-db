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

#include <random>

#include "yb/gutil/casts.h"

#include "yb/rocksdb/util/xxhash.h"

#include "yb/util/crc.h"
#include "yb/util/crc32c_accelerated.h"
#include "yb/util/format.h"
#include "yb/util/status_fwd.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_util.h"

using std::string;

namespace yb {
namespace crc {


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

uint32_t CrcutilCrc32c(const void* data, size_t length) {
  uint64_t crc = 0;
  GetCrc32cInstance()->Compute(data, length, &crc);
  return static_cast<uint32_t>(crc);
}

// Crc32c and Crc32Accumulator use the accelerated implementation when the CPU supports it, and must
// produce the same values as crcutil.
TEST_F(CrcTest, AcceleratedMatchesCrcutil) {
  constexpr size_t kMaxShortLength = 4096;
  std::unique_ptr<Crc32cEngine> engine(TryNewCRC32AcceleratedX86ARMCombined());
  LOG(INFO) << "Accelerated CRC32C available: " << (engine != nullptr);
#if defined(__x86_64__)
  ASSERT_NE(engine, nullptr);
#endif

  std::mt19937 rng(42);
  std::vector<uint8_t> data(128 * 1024);
  for (auto& byte : data) {
    byte = static_cast<uint8_t>(rng());
  }
  for (size_t offset : {0, 1, 3, 7, 13}) {
    const auto* start = data.data() + offset;
    for (size_t length = 0; length <= kMaxShortLength; ++length) {
      ASSERT_EQ(CrcutilCrc32c(start, length), Crc32c(start, length))
          << "offset: " << offset << ", length: " << length;
    }
    for (size_t length : {8191, 8192, 65535, 65536, 65540, 100000}) {
      ASSERT_EQ(CrcutilCrc32c(start, length), Crc32c(start, length))
          << "offset: " << offset << ", length: " << length;
    }
  }

  for (int i = 0; i != 100; ++i) {
    const size_t length = rng() % data.size();
    Crc32Accumulator accumulator;
    for (size_t pos = 0; pos < length;) {
      auto chunk = std::min<size_t>(length - pos, rng() % 1000);
      accumulator.Feed(data.data() + pos, chunk);
      pos += chunk;
    }
    ASSERT_EQ(CrcutilCrc32c(data.data(), length), accumulator.result()) << "length: " << length;
  }
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
