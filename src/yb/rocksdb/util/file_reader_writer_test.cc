//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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
#include <vector>

#include "yb/rocksdb/util/file_reader_writer.h"
#include "yb/rocksdb/util/random.h"
#include "yb/rocksdb/util/testharness.h"
#include "yb/rocksdb/util/testutil.h"

#include "yb/util/path_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace rocksdb {

class WritableFileWriterTest : public RocksDBTest {};

const uint32_t kMb = 1 << 20;

static const std::string kFakeWfFilename = "FakeWF";

TEST_F(WritableFileWriterTest, RangeSync) {
  class FakeWF : public WritableFile {
   public:
    FakeWF() : size_(0), last_synced_(0) {}
    ~FakeWF() {}

    Status Append(const Slice& data) override {
      size_ += data.size();
      return Status::OK();
    }
    Status Truncate(uint64_t size) override {
      return Status::OK();
    }
    Status Close() override {
      EXPECT_GE(size_, last_synced_ + kMb);
      EXPECT_LT(size_, last_synced_ + 2 * kMb);
      // Make sure random writes generated enough writes.
      EXPECT_GT(size_, 10 * kMb);
      return Status::OK();
    }
    Status Flush() override { return Status::OK(); }
    Status Sync() override { return Status::OK(); }
    Status Fsync() override { return Status::OK(); }
    void SetIOPriority(Env::IOPriority pri) override {}
    uint64_t GetFileSize() override { return size_; }
    void GetPreallocationStatus(size_t* block_size,
                                size_t* last_allocated_block) override {}
    size_t GetUniqueId(char* id) const override { return 0; }
    Status InvalidateCache(size_t offset, size_t length) override {
      return Status::OK();
    }
    const std::string& filename() const override { return kFakeWfFilename; }

   protected:
    Status Allocate(uint64_t offset, uint64_t len) override { return Status::OK(); }
    Status RangeSync(uint64_t offset, uint64_t nbytes) override {
      EXPECT_EQ(offset % 4096, 0u);
      EXPECT_EQ(nbytes % 4096, 0u);

      EXPECT_EQ(offset, last_synced_);
      last_synced_ = offset + nbytes;
      EXPECT_GE(size_, last_synced_ + kMb);
      if (size_ > 2 * kMb) {
        EXPECT_LT(size_, last_synced_ + 2 * kMb);
      }
      return Status::OK();
    }

    uint64_t size_;
    uint64_t last_synced_;
  };

  EnvOptions env_options;
  env_options.bytes_per_sync = kMb;
  std::unique_ptr<FakeWF> wf(new FakeWF);
  std::unique_ptr<WritableFileWriter> writer(
      new WritableFileWriter(std::move(wf), env_options));
  Random r(301);
  std::unique_ptr<char[]> large_buf(new char[10 * kMb]);
  for (int i = 0; i < 1000; i++) {
    int skew_limit = (i < 700) ? 10 : 15;
    uint32_t num = r.Skewed(skew_limit) * 100 + r.Uniform(100);
    ASSERT_OK(writer->Append(Slice(large_buf.get(), num)));

    // Flush in a chance of 1/10.
    if (r.Uniform(10) == 0) {
      ASSERT_OK(writer->Flush());
    }
  }
  ASSERT_OK(writer->Close());
}

TEST_F(WritableFileWriterTest, AppendStatusReturn) {
  class FakeWF : public WritableFile {
   public:
    FakeWF() : use_os_buffer_(true), io_error_(false) {}

    bool UseOSBuffer() const override { return use_os_buffer_; }
    Status Append(const Slice& data) override {
      if (io_error_) {
        return STATUS(IOError, "Fake IO error");
      }
      return Status::OK();
    }
    Status PositionedAppend(const Slice& data, uint64_t) override {
      if (io_error_) {
        return STATUS(IOError, "Fake IO error");
      }
      return Status::OK();
    }
    Status Close() override { return Status::OK(); }
    Status Flush() override { return Status::OK(); }
    Status Sync() override { return Status::OK(); }
    void SetUseOSBuffer(bool val) { use_os_buffer_ = val; }
    void SetIOError(bool val) { io_error_ = val; }
    const std::string& filename() const override { return kFakeWfFilename; }

   protected:
    bool use_os_buffer_;
    bool io_error_;
  };
  std::unique_ptr<FakeWF> wf(new FakeWF());
  wf->SetUseOSBuffer(false);
  std::unique_ptr<WritableFileWriter> writer(
      new WritableFileWriter(std::move(wf), EnvOptions()));

  ASSERT_OK(writer->Append(std::string(2 * kMb, 'a')));

  // Next call to WritableFile::Append() should fail
  dynamic_cast<FakeWF*>(writer->writable_file())->SetIOError(true);
  ASSERT_NOK(writer->Append(std::string(2 * kMb, 'b')));
}

TEST(FileReaderWriterTest, CheckFileTailForZeros) {
  constexpr auto kNonZeroPrefixSize = 123;

  Random r(301);
  uint8_t prefix[kNonZeroPrefixSize];
  for (auto i = 0; i < kNonZeroPrefixSize; ++i) {
    prefix[i] = 1 + r.Uniform(255);
  }
  std::vector<uint8_t> zeros(32_KB, 0);

  auto* env = Env::Default();

  const auto test_dir = yb::GetTestDataDirectory();
  const auto file_path = yb::JoinPathSegments(test_dir, "file");

  for (auto num_tail_zeros_base : {0_KB, 1_KB, 2_KB, 8_KB, 16_KB}) {
    for (auto num_tail_zeros = num_tail_zeros_base > 32 ? num_tail_zeros_base - 32 : 0;
         num_tail_zeros < num_tail_zeros_base + 32;
         ++num_tail_zeros) {
      if (num_tail_zeros > zeros.size()) {
        zeros.resize(num_tail_zeros, 0);
      }

      std::unique_ptr<WritableFile> file;
      ASSERT_OK(env->NewWritableFile(file_path, &file, EnvOptions()));
      ASSERT_OK(file->Append(Slice(prefix, kNonZeroPrefixSize)));
      ASSERT_OK(file->Append(Slice(zeros.data(), num_tail_zeros)));
      ASSERT_OK(file->Close());

      for (auto tail_bytes_to_check_base : {0_KB, 1_KB, 2_KB, 8_KB, 16_KB}) {
        for (auto tail_bytes_to_check =
                 tail_bytes_to_check_base > 32 ? tail_bytes_to_check_base - 32 : 0;
             tail_bytes_to_check < tail_bytes_to_check_base + 32;
             ++tail_bytes_to_check) {
          auto s = CheckFileTailForZeros(env, EnvOptions(), file_path, tail_bytes_to_check);
          if (tail_bytes_to_check > num_tail_zeros) {
            // We check tail to have more zeros than actually written to the end of the file,
            // tail_bytes_to_check zeros shouldn't be detected.
            ASSERT_TRUE(s.ok()) << "num_tail_zeros: " << num_tail_zeros
                                << " tail_bytes_to_check: " << tail_bytes_to_check;
          } else {
            // Should detect that there are at least tail_bytes_to_check zeros at the end of the
            // file.
            ASSERT_NOK(s) << "num_tail_zeros: " << num_tail_zeros
                          << " tail_bytes_to_check: " << tail_bytes_to_check;
            // Should calculate number of zeros at the end of the file correctly.
            ASSERT_TRUE(s.ToString().ends_with(yb::Format(
                "last $0 of $1 bytes are all zeros",
                num_tail_zeros,
                kNonZeroPrefixSize + num_tail_zeros)))
                << "num_tail_zeros: " << num_tail_zeros << " status: " << s;
          }
        }
      }
    }
  }
}

}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
