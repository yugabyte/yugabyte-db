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
// Micro benchmark for writing/reading bit streams and Kudu specific
// run-length encoding (RLE) APIs. Currently only covers booleans and
// the most performance sensitive APIs. NB: Impala contains a RLE
// micro benchmark (rle-benchmark.cc).
//

#include <glog/logging.h>
#include <gflags/gflags.h>

#include "kudu/gutil/mathlimits.h"
#include "kudu/util/bit-stream-utils.h"
#include "kudu/util/logging.h"
#include "kudu/util/rle-encoding.h"
#include "kudu/util/stopwatch.h"

DEFINE_int32(bitstream_num_bytes, 1 * 1024 * 1024,
             "Number of bytes worth of bits to write and read from the bitstream");

namespace kudu {

// Measure writing and reading single-bit streams
void BooleanBitStream() {
  faststring buffer(FLAGS_bitstream_num_bytes);
  BitWriter writer(&buffer);

  // Write alternating strings of repeating 0's and 1's
  for (int i = 0; i < FLAGS_bitstream_num_bytes; ++i) {
    writer.PutValue(i % 2, 1);
    writer.PutValue(i % 2, 1);
    writer.PutValue(i % 2, 1);
    writer.PutValue(i % 2, 1);
    writer.PutValue(i % 2, 1);
    writer.PutValue(i % 2, 1);
    writer.PutValue(i % 2, 1);
    writer.PutValue(i % 2, 1);
  }
  writer.Flush();

  LOG(INFO) << "Wrote " << writer.bytes_written() << " bytes";

  BitReader reader(buffer.data(), writer.bytes_written());
  for (int i = 0; i < FLAGS_bitstream_num_bytes; ++i) {
    bool val;
    reader.GetValue(1, &val);
    reader.GetValue(1, &val);
    reader.GetValue(1, &val);
    reader.GetValue(1, &val);
    reader.GetValue(1, &val);
    reader.GetValue(1, &val);
    reader.GetValue(1, &val);
    reader.GetValue(1, &val);
  }
}

// Measure bulk puts and decoding runs of RLE bools
void BooleanRLE() {
  const int num_iters = 3 * 1024;

  faststring buffer(45 * 1024);
  RleEncoder<bool> encoder(&buffer, 1);

  for (int i = 0; i < num_iters; i++) {
    encoder.Put(false, 100 * 1024);
    encoder.Put(true, 3);
    encoder.Put(false, 3);
    encoder.Put(true, 213 * 1024);
    encoder.Put(false, 300);
    encoder.Put(true, 8);
    encoder.Put(false, 4);
  }

  LOG(INFO) << "Wrote " << encoder.len() << " bytes";

  RleDecoder<bool> decoder(buffer.data(), encoder.len(), 1);
  bool val = false;
  size_t run_length;
  for (int i = 0; i < num_iters; i++) {
    run_length = decoder.GetNextRun(&val, MathLimits<size_t>::kMax);
    run_length = decoder.GetNextRun(&val, MathLimits<size_t>::kMax);
    run_length = decoder.GetNextRun(&val, MathLimits<size_t>::kMax);
    run_length = decoder.GetNextRun(&val, MathLimits<size_t>::kMax);
    run_length = decoder.GetNextRun(&val, MathLimits<size_t>::kMax);
    run_length = decoder.GetNextRun(&val, MathLimits<size_t>::kMax);
    run_length = decoder.GetNextRun(&val, MathLimits<size_t>::kMax);
  }
}

} // namespace kudu

int main(int argc, char **argv) {
  FLAGS_logtostderr = 1;
  google::ParseCommandLineFlags(&argc, &argv, true);
  kudu::InitGoogleLoggingSafe(argv[0]);

  LOG_TIMING(INFO, "BooleanBitStream") {
    kudu::BooleanBitStream();
  }

  LOG_TIMING(INFO, "BooleanRLE") {
    kudu::BooleanRLE();
  }

  return 0;
}
