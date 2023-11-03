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

#include <fcntl.h>
#include <sys/types.h>

#include <memory>
#include <string>

#include "yb/util/logging.h"
#include <gtest/gtest.h>

#include "yb/gutil/bind.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/strings/util.h"
#include "yb/util/alignment.h"
#include "yb/util/stol_utils.h"
#include "yb/util/crc.h"
#include "yb/util/env.h"
#include "yb/util/env_util.h"
#include "yb/util/memenv/memenv.h"
#include "yb/util/os-util.h"
#include "yb/util/random.h"
#include "yb/util/random_util.h"
#include "yb/util/path_util.h"
#include "yb/util/status.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"

DECLARE_int32(o_direct_block_size_bytes);
DECLARE_bool(TEST_simulate_fs_without_fallocate);

#if !defined(__APPLE__)
#include <linux/falloc.h>
#endif  // !defined(__APPLE__)
// Copied from falloc.h. Useful for older kernels that lack support for
// hole punching; fallocate(2) will return EOPNOTSUPP.
#ifndef FALLOC_FL_KEEP_SIZE
#define FALLOC_FL_KEEP_SIZE 0x01 /* default is extend size */
#endif
#ifndef FALLOC_FL_PUNCH_HOLE
#define FALLOC_FL_PUNCH_HOLE  0x02 /* de-allocates range */
#endif

using namespace std::placeholders;

namespace yb {

using std::shared_ptr;
using std::string;
using std::vector;

static const uint64_t kOneMb = 1024 * 1024;

class TestEnv : public YBTest, public ::testing::WithParamInterface<bool> {
 public:
  void SetUp() override {
    YBTest::SetUp();
    CheckFallocateSupport();
  }

  // Verify that fallocate() is supported in the test directory.
  // Some local file systems like ext3 do not support it, and we don't
  // want to fail tests on those systems.
  //
  // Sets fallocate_supported_ based on the result.
  void CheckFallocateSupport() {
    static bool checked = false;
    if (checked) return;

    if (FLAGS_TEST_simulate_fs_without_fallocate) {
      checked = true;
      return;
    }

#if defined(__linux__)
    int fd = creat(GetTestPath("check-fallocate").c_str(), S_IWUSR);
    PCHECK(fd >= 0);
    int err = fallocate(fd, 0, 0, 4096);
    if (err != 0) {
      PCHECK(errno == ENOTSUP);
    } else {
      fallocate_supported_ = true;

      err = fallocate(fd, FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE,
                      1024, 1024);
      if (err != 0) {
        PCHECK(errno == ENOTSUP);
      } else {
        fallocate_punch_hole_supported_ = true;
      }
    }

    close(fd);
#endif

    checked = true;
  }

 protected:

  void VerifyTestData(const Slice& read_data, size_t offset) {
    for (size_t i = 0; i < read_data.size(); i++) {
      size_t file_offset = offset + i;
      ASSERT_EQ((file_offset * 31) & 0xff, read_data[i]) << "failed at " << i;
    }
  }

  void VerifyChecksumsMatch(const string file_path, size_t file_size, uint64_t expected_checksum,
                            const crc::Crc* crc32c) {
    shared_ptr<RandomAccessFile> raf;
    ASSERT_OK(env_util::OpenFileForRandom(env_.get(), file_path, &raf));
    Slice slice;
    std::unique_ptr<uint8_t[]> scratch(new uint8_t[file_size]);
    ASSERT_OK(env_util::ReadFully(raf.get(), 0, file_size, &slice, scratch.get()));
    ASSERT_EQ(file_size, slice.size());
    uint64_t file_checksum = 0;
    crc32c->Compute(slice.data(), slice.size(), &file_checksum);
    ASSERT_EQ(file_checksum, expected_checksum) << "File checksum didn't match expected checksum";
  }

  void MakeVectors(size_t num_slices, size_t slice_size, size_t num_iterations,
                   std::unique_ptr<faststring[]>* data, vector<vector<Slice > >* vec) {
    data->reset(new faststring[num_iterations * num_slices]);
    vec->resize(num_iterations);

    int data_idx = 0;
    int byte_idx = 0;
    for (size_t vec_idx = 0; vec_idx < num_iterations; vec_idx++) {
      vector<Slice>& iter_vec = vec->at(vec_idx);
      iter_vec.resize(num_slices);
      for (size_t i = 0; i < num_slices; i++) {
        (*data)[data_idx].resize(slice_size);
        for (size_t j = 0; j < slice_size; j++) {
          (*data)[data_idx][j] = (byte_idx * 31) & 0xff;
          ++byte_idx;
        }
        iter_vec[i]= Slice((*data)[data_idx]);
        ++data_idx;
      }
    }
  }

  void ReadAndVerifyTestData(RandomAccessFile* raf, size_t offset, size_t n) {
    std::unique_ptr<uint8_t[]> scratch(new uint8_t[n]);
    Slice s;
    ASSERT_OK(env_util::ReadFully(raf, offset, n, &s,
                                         scratch.get()));
    ASSERT_EQ(n, s.size());
    ASSERT_NO_FATALS(VerifyTestData(s, offset));
  }

  void TestAppendVector(size_t num_slices, size_t slice_size, size_t iterations,
                        bool fast, bool pre_allocate, const WritableFileOptions& opts) {
    const string kTestPath = GetTestPath("test_env_appendvec_read_append");
    shared_ptr<WritableFile> file;
    ASSERT_OK(env_util::OpenFileForWrite(opts, env_.get(), kTestPath, &file));

    if (pre_allocate) {
      ASSERT_OK(file->PreAllocate(num_slices * slice_size * iterations));
      ASSERT_OK(file->Sync());
    }

    std::unique_ptr<faststring[]> data;
    vector<vector<Slice> > input;

    MakeVectors(num_slices, slice_size, iterations, &data, &input);

    shared_ptr<RandomAccessFile> raf;

    if (!fast) {
      ASSERT_OK(env_util::OpenFileForRandom(env_.get(), kTestPath, &raf));
    }

    srand(123);

    const string test_descr = strings::Substitute(
        "appending a vector of slices(number of slices=$0,size of slice=$1 b) $2 times",
        num_slices, slice_size, iterations);
    LOG_TIMING(INFO, test_descr)  {
      for (size_t i = 0; i < iterations; i++) {
        if (fast || random() % 2) {
          ASSERT_OK(file->AppendVector(input[i]));
        } else {
          for (const Slice& slice : input[i]) {
            ASSERT_OK(file->Append(slice));
          }
        }
        if (!fast) {
          // Verify as write. Note: this requires that file is pre-allocated, otherwise
          // the ReadFully() fails with EINVAL.
          if (opts.o_direct) {
            ASSERT_OK(file->Sync());
          }
          ASSERT_NO_FATALS(ReadAndVerifyTestData(raf.get(), num_slices * slice_size * i,
                                                        num_slices * slice_size));
        }
      }
    }

    // Verify the entire file
    ASSERT_OK(file->Close());

    if (fast) {
      ASSERT_OK(env_util::OpenFileForRandom(env_.get(), kTestPath, &raf));
    }
    for (size_t i = 0; i < iterations; i++) {
      ASSERT_NO_FATALS(ReadAndVerifyTestData(raf.get(), num_slices * slice_size * i,
                                                    num_slices * slice_size));
    }
  }

  void TestAppendRandomData(bool pre_allocate, const WritableFileOptions& opts) {
    const string kTestPath = GetTestPath("test_env_append_random_read_append");
    const uint64_t kMaxFileSize = 64 * 1024 * 1024;
    shared_ptr<WritableFile> file;
    ASSERT_OK(env_util::OpenFileForWrite(opts, env_.get(), kTestPath, &file));

    if (pre_allocate) {
      ASSERT_OK(file->PreAllocate(kMaxFileSize));
      ASSERT_OK(file->Sync());
    }

    Random rnd(SeedRandom());

    size_t total_size = 0;
    const int kBufSize = (IOV_MAX + 10) * FLAGS_o_direct_block_size_bytes;
    char buf[kBufSize];
    crc::Crc* crc32c = crc::GetCrc32cInstance();
    uint64_t actual_checksum = 0;

    while (true) {
      auto i = rnd.Uniform(10);
      size_t slice_size;
      if (i < 4) {
        // 40% of the time pick a size between 1 and FLAGS_o_direct_block_size_bytes.
        slice_size = rnd.Uniform(FLAGS_o_direct_block_size_bytes) + 1;
      } else if (i >= 4 && i < 8) {
        // 40% of the time pick a size between block_size and 10*FLAGS_o_direct_block_size_bytes
        // that is a multiple of FLAGS_o_direct_block_size_bytes.
        slice_size = (rnd.Uniform(10) + 1) * FLAGS_o_direct_block_size_bytes;
      } else if (i == 8) {
        // 10% of the time pick a size greater than the IOV_MAX * FLAGS_o_direct_block_size_bytes.
        slice_size = IOV_MAX * FLAGS_o_direct_block_size_bytes +
            rnd.Uniform(10 * FLAGS_o_direct_block_size_bytes);
      } else {
        // 10% of the time pick a size such that the file size after writing this slice is a
        // multiple of FLAGS_o_direct_block_size_bytes.
        auto bytes_needed = align_up(total_size, FLAGS_o_direct_block_size_bytes) - total_size;
        slice_size = FLAGS_o_direct_block_size_bytes + bytes_needed;
      }
      if (total_size + slice_size > kMaxFileSize) {
        break;
      }
      total_size += slice_size;

      RandomString(buf, slice_size, &rnd);
      auto slice = Slice(buf, slice_size);
      ASSERT_OK(file->Append(slice));
      ASSERT_EQ(total_size, file->Size());

      // Compute a rolling checksum over the two byte arrays (size, body).
      crc32c->Compute(slice.data(), slice.size(), &actual_checksum);

      if (rnd.Uniform(5) == 0) {
        ASSERT_OK(file->Sync());
        ASSERT_EQ(total_size, file->Size());
      }
    }

    // Verify the entire file
    ASSERT_OK(file->Close());
    ASSERT_NO_FATALS(VerifyChecksumsMatch(kTestPath, total_size, actual_checksum, crc32c));
  }

  static bool fallocate_supported_;
  static bool fallocate_punch_hole_supported_;
};

bool TestEnv::fallocate_supported_ = false;
bool TestEnv::fallocate_punch_hole_supported_ = false;

TEST_P(TestEnv, TestPreallocate) {
  if (!fallocate_supported_) {
    LOG(INFO) << "fallocate not supported, skipping test";
    return;
  }
  LOG(INFO) << "Testing PreAllocate()";
  string test_path = GetTestPath("test_env_wf");
  shared_ptr<WritableFile> file;
  WritableFileOptions opts;
  opts.o_direct = GetParam();
  ASSERT_OK(env_util::OpenFileForWrite(opts,
                                       env_.get(), test_path, &file));

  // pre-allocate 1 MB
  ASSERT_OK(file->PreAllocate(kOneMb));
  ASSERT_OK(file->Sync());

  // the writable file size should report 0
  ASSERT_EQ(file->Size(), 0);
  // but the real size of the file on disk should report 1MB
  uint64_t size = ASSERT_RESULT(env_->GetFileSize(test_path));
  ASSERT_EQ(size, kOneMb);

  // write 1 MB
  uint8_t scratch[kOneMb];
  Slice slice(scratch, kOneMb);
  ASSERT_OK(file->Append(slice));
  ASSERT_OK(file->Sync());

  // the writable file size should now report 1 MB
  ASSERT_EQ(file->Size(), kOneMb);
  ASSERT_OK(file->Close());
  // and the real size for the file on disk should match ony the
  // written size
  size = ASSERT_RESULT(env_->GetFileSize(test_path));
  ASSERT_EQ(kOneMb, size);
}

// PosixDirectIOWritableFile is only avaliable on linux
#if defined(__linux__)
TEST_F(TestEnv, TestReopenDirectIOWritableFile) {
  string test_path = GetTestPath("test_env_wf");
  Random rnd(SeedRandom());
  const size_t data_size = FLAGS_o_direct_block_size_bytes + 1;
  std::unique_ptr<uint8_t[]> scratch(new uint8_t[data_size]);
  RandomString(scratch.get(), data_size, &rnd);
  Slice data(scratch.get(), data_size);

  // Create the file and write data to it.
  shared_ptr<WritableFile> writer;
  WritableFileOptions open_opts = WritableFileOptions();
  open_opts.o_direct = true;
  ASSERT_OK(env_util::OpenFileForWrite(open_opts,
                                       env_.get(), test_path, &writer));
  ASSERT_OK(writer->Append(data));
  ASSERT_OK(writer->Sync());
  // There should be two blocks on disk. The first full blocks has size
  // FLAGS_o_direct_block_size_bytes, and second incomplete block has size 1.
  ASSERT_EQ(data_size, writer->Size());
  // The file size on disk should be FLAGS_o_direct_block_size_bytes * 2,
  // since every sync will write a multiple of the block size.
  uint64_t size = ASSERT_RESULT(env_->GetFileSize(test_path));
  ASSERT_EQ(FLAGS_o_direct_block_size_bytes * 2, size);
  ASSERT_OK(writer->Close());

  // Reopen the file with size that is not a multiple of the block size.
  shared_ptr<WritableFile> writer2;
  open_opts.mode = Env::OPEN_EXISTING;
  open_opts.initial_offset = data_size;
  ASSERT_OK(env_util::OpenFileForWrite(open_opts,
                                       env_.get(), test_path, &writer2));
  ASSERT_EQ(data_size, writer2->Size());
  uint8_t scratch_one_byte[1] = {0x55};
  Slice one_byte(scratch_one_byte, 1);
  ASSERT_OK(writer2->Append(one_byte));
  ASSERT_OK(writer2->Sync());
  ASSERT_EQ(data_size + 1, writer2->Size());
  size = ASSERT_RESULT(env_->GetFileSize(test_path));
  ASSERT_EQ(FLAGS_o_direct_block_size_bytes * 2, size);

  // Verify the data.
  std::unique_ptr<RandomAccessFile> readable_file;
  ASSERT_OK(Env::Default()->NewRandomAccessFile(test_path, &readable_file));
  std::unique_ptr<uint8_t[]> scratch_after_reopen(new uint8_t[data_size+1]);
  Slice data_after_reopen;
  ASSERT_OK(env_util::ReadFully(readable_file.get(), 0, data_size+1,
                                &data_after_reopen, scratch_after_reopen.get()));
  ASSERT_EQ(data.ToBuffer()+one_byte.ToBuffer(), data_after_reopen.ToBuffer());
}
#endif

// To test consecutive pre-allocations we need higher pre-allocations since the
// mmapped regions grow in size until 2MBs (so smaller pre-allocations will easily
// be smaller than the mmapped regions size).
TEST_F(TestEnv, TestConsecutivePreallocate) {
  if (!fallocate_supported_) {
    LOG(INFO) << "fallocate not supported, skipping test";
    return;
  }
  LOG(INFO) << "Testing consecutive PreAllocate()";
  string test_path = GetTestPath("test_env_wf");
  shared_ptr<WritableFile> file;
  ASSERT_OK(env_util::OpenFileForWrite(
      WritableFileOptions(), env_.get(), test_path, &file));

  // pre-allocate 64 MB
  ASSERT_OK(file->PreAllocate(64 * kOneMb));
  ASSERT_OK(file->Sync());

  // the writable file size should report 0
  ASSERT_EQ(file->Size(), 0);
  // but the real size of the file on disk should report 64 MBs
  uint64_t size = ASSERT_RESULT(env_->GetFileSize(test_path));
  ASSERT_EQ(size, 64 * kOneMb);

  // write 1 MB
  uint8_t scratch[kOneMb];
  Slice slice(scratch, kOneMb);
  ASSERT_OK(file->Append(slice));
  ASSERT_OK(file->Sync());

  // the writable file size should now report 1 MB
  ASSERT_EQ(kOneMb, file->Size());
  size = ASSERT_RESULT(env_->GetFileSize(test_path));
  ASSERT_EQ(64 * kOneMb, size);

  // pre-allocate 64 additional MBs
  ASSERT_OK(file->PreAllocate(64 * kOneMb));
  ASSERT_OK(file->Sync());

  // the writable file size should now report 1 MB
  ASSERT_EQ(kOneMb, file->Size());
  // while the real file size should report 128 MB's
  size = ASSERT_RESULT(env_->GetFileSize(test_path));
  ASSERT_EQ(128 * kOneMb, size);

  // write another MB
  ASSERT_OK(file->Append(slice));
  ASSERT_OK(file->Sync());

  // the writable file size should now report 2 MB
  ASSERT_EQ(file->Size(), 2 * kOneMb);
  // while the real file size should reamin at 128 MBs
  size = ASSERT_RESULT(env_->GetFileSize(test_path));
  ASSERT_EQ(128 * kOneMb, size);

  // close the file (which ftruncates it to the real size)
  ASSERT_OK(file->Close());
  // and the real size for the file on disk should match only the written size
  size = ASSERT_RESULT(env_->GetFileSize(test_path));
  ASSERT_EQ(2* kOneMb, size);

}

TEST_F(TestEnv, TestHolePunch) {
  if (!fallocate_punch_hole_supported_) {
    LOG(INFO) << "hole punching not supported, skipping test";
    return;
  }
  string test_path = GetTestPath("test_env_wf");
  std::unique_ptr<RWFile> file;
  ASSERT_OK(env_->NewRWFile(test_path, &file));

  // Write 1 MB. The size and size-on-disk both agree.
  uint8_t scratch[kOneMb];
  Slice slice(scratch, kOneMb);
  ASSERT_OK(file->Write(0, slice));
  ASSERT_OK(file->Sync());
  uint64_t sz;
  ASSERT_OK(file->Size(&sz));
  ASSERT_EQ(kOneMb, sz);
  uint64_t size_on_disk = ASSERT_RESULT(env_->GetFileSizeOnDisk(test_path));
  // Some kernels and filesystems (e.g. Centos 6.6 with XFS) aggressively
  // preallocate file disk space when writing to files, so the disk space may be
  // greater than 1MiB.
  ASSERT_LE(kOneMb, size_on_disk);

  // Punch some data out at byte marker 4096. Now the two sizes diverge.
  uint64_t punch_amount = 4096 * 4;
  uint64_t new_size_on_disk;
  ASSERT_OK(file->PunchHole(4096, punch_amount));
  ASSERT_OK(file->Size(&sz));
  ASSERT_EQ(kOneMb, sz);
  new_size_on_disk = ASSERT_RESULT(env_->GetFileSizeOnDisk(test_path));
  ASSERT_EQ(size_on_disk - punch_amount, new_size_on_disk);
}

class ShortReadRandomAccessFile : public RandomAccessFile {
 public:
  explicit ShortReadRandomAccessFile(shared_ptr<RandomAccessFile> wrapped)
      : wrapped_(std::move(wrapped)), seed_(SeedRandom()) {}

  virtual Status Read(uint64_t offset, size_t n, Slice* result,
                      uint8_t *scratch) const override {
    CHECK_GT(n, 0);
    // Divide the requested amount of data by a small integer,
    // and issue the shorter read to the underlying file.
    auto short_n = n / ((rand_r(&seed_) % 3) + 1);
    if (short_n == 0) {
      short_n = 1;
    }

    VLOG(1) << "Reading " << short_n << " instead of " << n;

    return wrapped_->Read(offset, short_n, result, scratch);
  }

  Result<uint64_t> Size() const override {
    return wrapped_->Size();
  }

  Result<uint64_t> INode() const override {
    return wrapped_->INode();
  }

  const string& filename() const override { return wrapped_->filename(); }

  size_t memory_footprint() const override {
    return wrapped_->memory_footprint();
  }

 private:
  const shared_ptr<RandomAccessFile> wrapped_;
  mutable unsigned int seed_;
};

// Write 'size' bytes of data to a file, with a simple pattern stored in it.
static void WriteTestFile(Env* env, const string& path, size_t size) {
  shared_ptr<WritableFile> wf;
  ASSERT_OK(env_util::OpenFileForWrite(env, path, &wf));
  faststring data;
  data.resize(size);
  for (size_t i = 0; i < data.size(); i++) {
    data[i] = (i * 31) & 0xff;
  }
  ASSERT_OK(wf->Append(Slice(data)));
  ASSERT_OK(wf->Close());
}

TEST_F(TestEnv, TestReadFully) {
  SeedRandom();
  const string kTestPath = "test";
  const int kFileSize = 64 * 1024;
  std::unique_ptr<Env> mem(NewMemEnv(Env::Default()));

  WriteTestFile(mem.get(), kTestPath, kFileSize);
  ASSERT_NO_FATALS();

  // Reopen for read
  shared_ptr<RandomAccessFile> raf;
  ASSERT_OK(env_util::OpenFileForRandom(mem.get(), kTestPath, &raf));

  ShortReadRandomAccessFile sr_raf(raf);

  const int kReadLength = 10000;
  Slice s;
  std::unique_ptr<uint8_t[]> scratch(new uint8_t[kReadLength]);

  // Verify that ReadFully reads the whole requested data.
  ASSERT_OK(env_util::ReadFully(&sr_raf, 0, kReadLength, &s, scratch.get()));
  ASSERT_EQ(s.data(), scratch.get()) << "Should have returned a contiguous copy";
  ASSERT_EQ(kReadLength, s.size());

  // Verify that the data read was correct.
  VerifyTestData(s, 0);

  // Verify that ReadFully fails with an IOError at EOF.
  Status status = env_util::ReadFully(&sr_raf, kFileSize - 100, 200, &s, scratch.get());
  ASSERT_FALSE(status.ok());
  ASSERT_TRUE(status.IsIOError());
  ASSERT_STR_CONTAINS(status.ToString(), "EOF");
}

TEST_P(TestEnv, TestAppendVector) {
  WritableFileOptions opts;
  opts.o_direct = GetParam();
  LOG(INFO) << "Testing AppendVector() only, NO pre-allocation";
  ASSERT_NO_FATALS(TestAppendVector(2000, 1024, 5, true, false, opts));

  if (!fallocate_supported_) {
    LOG(INFO) << "fallocate not supported, skipping preallocated runs";
  } else {
    LOG(INFO) << "Testing AppendVector() only, WITH pre-allocation";
    ASSERT_NO_FATALS(TestAppendVector(2000, 1024, 5, true, true, opts));
    LOG(INFO) << "Testing AppendVector() together with Append() and Read(), WITH pre-allocation";
    ASSERT_NO_FATALS(TestAppendVector(128, 4096, 5, false, true, opts));
  }
}

TEST_F(TestEnv, TestRandomData) {
  WritableFileOptions opts;
  opts.o_direct = true;
  LOG(INFO) << "Testing Append() with random data and requests of random sizes";
  ASSERT_NO_FATALS(TestAppendRandomData(true, opts));
}

TEST_F(TestEnv, TestGetExecutablePath) {
  string p;
  ASSERT_OK(Env::Default()->GetExecutablePath(&p));
  ASSERT_TRUE(HasSuffixString(p, "env-test")) << p;
}

TEST_F(TestEnv, TestOpenEmptyRandomAccessFile) {
  Env* env = Env::Default();
  string test_file = JoinPathSegments(GetTestDataDirectory(), "test_file");
  ASSERT_NO_FATALS(WriteTestFile(env, test_file, 0));
  std::unique_ptr<RandomAccessFile> readable_file;
  ASSERT_OK(env->NewRandomAccessFile(test_file, &readable_file));
  uint64_t size = ASSERT_RESULT(readable_file->Size());
  ASSERT_EQ(0, size);
}

TEST_F(TestEnv, TestOverwrite) {
  string test_path = GetTestPath("test_env_wf");

  // File does not exist, create it.
  shared_ptr<WritableFile> writer;
  ASSERT_OK(env_util::OpenFileForWrite(env_.get(), test_path, &writer));

  // File exists, overwrite it.
  ASSERT_OK(env_util::OpenFileForWrite(env_.get(), test_path, &writer));

  // File exists, try to overwrite (and fail).
  WritableFileOptions opts;
  opts.mode = Env::CREATE_NON_EXISTING;
  Status s = env_util::OpenFileForWrite(opts,
                                        env_.get(), test_path, &writer);
  ASSERT_TRUE(s.IsAlreadyPresent());
}

TEST_F(TestEnv, TestReopenWritableFileWithInitialOffsetOption) {
  LOG(INFO) << "Testing opening behavior with setting initial offset";
  string test_path = GetTestPath("test_env_wf");
  string data = "The quick brown fox";

  // Create the file and write data to it.
  shared_ptr<WritableFile> writer;
  WritableFileOptions open_opts = WritableFileOptions();
  ASSERT_OK(env_util::OpenFileForWrite(open_opts,
                                       env_.get(), test_path, &writer));
  ASSERT_OK(writer->PreAllocate(kOneMb));
  ASSERT_OK(writer->Append(data));
  ASSERT_EQ(data.length(), writer->Size());

  // Open the file, we should expect the writer2 begin with OneMb size
  // because of the preallocate.
  shared_ptr<WritableFile> writer2;
  open_opts.mode = Env::OPEN_EXISTING;
  ASSERT_OK(env_util::OpenFileForWrite(open_opts,
                                       env_.get(), test_path, &writer2));
  ASSERT_EQ(kOneMb, writer2->Size());
  // Now open the file again with having initial offset, we should expect
  // the file start at data.length().
  ASSERT_OK(writer2->Close());
  open_opts.initial_offset = data.length();
  ASSERT_OK(env_util::OpenFileForWrite(open_opts,
                                       env_.get(), test_path, &writer2));
  ASSERT_EQ(data.length(), writer2->Size());
  // Check the actual size on disk, it should be kOneMb.
  uint64_t size_on_disk = ASSERT_RESULT(env_->GetFileSize(test_path));
  ASSERT_EQ(kOneMb, size_on_disk);
  ASSERT_OK(writer2->Close());

  // Specified offset is less than the data.length()
  open_opts.initial_offset = data.length()-1;
  ASSERT_OK(env_util::OpenFileForWrite(open_opts,
                                       env_.get(), test_path, &writer2));
  ASSERT_EQ(data.length()-1, writer2->Size());
  // Previous Close() has truncated file to size data.length().
  size_on_disk = ASSERT_RESULT(env_->GetFileSize(test_path));
  ASSERT_EQ(data.length(), size_on_disk);
}

TEST_F(TestEnv, TestReopen) {
  LOG(INFO) << "Testing reopening behavior";
  string test_path = GetTestPath("test_env_wf");
  string first = "The quick brown fox";
  string second = "jumps over the lazy dog";

  // Create the file and write to it.
  shared_ptr<WritableFile> writer;
  ASSERT_OK(env_util::OpenFileForWrite(WritableFileOptions(),
                                       env_.get(), test_path, &writer));
  ASSERT_OK(writer->Append(first));
  ASSERT_EQ(first.length(), writer->Size());
  ASSERT_OK(writer->Close());

  // Reopen it and append to it.
  WritableFileOptions reopen_opts;
  reopen_opts.mode = Env::OPEN_EXISTING;
  ASSERT_OK(env_util::OpenFileForWrite(reopen_opts,
                                       env_.get(), test_path, &writer));
  ASSERT_EQ(first.length(), writer->Size());
  ASSERT_OK(writer->Append(second));
  ASSERT_EQ(first.length() + second.length(), writer->Size());
  ASSERT_OK(writer->Close());

  // Check that the file has both strings.
  shared_ptr<RandomAccessFile> reader;
  ASSERT_OK(env_util::OpenFileForRandom(env_.get(), test_path, &reader));
  uint64_t size = ASSERT_RESULT(reader->Size());
  ASSERT_EQ(first.length() + second.length(), size);
  Slice s;
  std::vector<uint8_t> scratch(size);
  ASSERT_OK(env_util::ReadFully(reader.get(), 0, size, &s, scratch.data()));
  ASSERT_EQ(first + second, s.ToString());
}

TEST_F(TestEnv, TestIsDirectory) {
  string dir = GetTestPath("a_directory");
  ASSERT_OK(env_->CreateDir(dir));
  bool is_dir;
  ASSERT_OK(env_->IsDirectory(dir, &is_dir));
  ASSERT_TRUE(is_dir);

  string not_dir = GetTestPath("not_a_directory");
  std::unique_ptr<WritableFile> writer;
  ASSERT_OK(env_->NewWritableFile(not_dir, &writer));
  ASSERT_OK(env_->IsDirectory(not_dir, &is_dir));
  ASSERT_FALSE(is_dir);
}

static Status TestWalkCb(vector<string>* actual,
                         Env::FileType type,
                         const string& dirname, const string& basename) {
  VLOG(1) << type << ":" << dirname << ":" << basename;
  actual->push_back(JoinPathSegments(dirname, basename));
  return Status::OK();
}

static Status CreateDir(Env* env, const string& name, vector<string>* created) {
  RETURN_NOT_OK(env->CreateDir(name));
  created->push_back(name);
  return Status::OK();
}

static Status CreateFile(Env* env, const string& name, vector<string>* created) {
  std::unique_ptr<WritableFile> writer;
  RETURN_NOT_OK(env->NewWritableFile(name, &writer));
  created->push_back(writer->filename());
  return Status::OK();
}

TEST_F(TestEnv, TestWalk) {
  // We test with this tree:
  //
  // /root/
  // /root/file_1
  // /root/file_2
  // /root/dir_a/file_1
  // /root/dir_a/file_2
  // /root/dir_b/file_1
  // /root/dir_b/file_2
  // /root/dir_b/dir_c/file_1
  // /root/dir_b/dir_c/file_2
  string root = GetTestPath("root");
  string subdir_a = JoinPathSegments(root, "dir_a");
  string subdir_b = JoinPathSegments(root, "dir_b");
  string subdir_c = JoinPathSegments(subdir_b, "dir_c");
  string file_one = "file_1";
  string file_two = "file_2";
  vector<string> expected;
  ASSERT_OK(CreateDir(env_.get(), root, &expected));
  ASSERT_OK(CreateFile(env_.get(), JoinPathSegments(root, file_one), &expected));
  ASSERT_OK(CreateFile(env_.get(), JoinPathSegments(root, file_two), &expected));
  ASSERT_OK(CreateDir(env_.get(), subdir_a, &expected));
  ASSERT_OK(CreateFile(env_.get(), JoinPathSegments(subdir_a, file_one), &expected));
  ASSERT_OK(CreateFile(env_.get(), JoinPathSegments(subdir_a, file_two), &expected));
  ASSERT_OK(CreateDir(env_.get(), subdir_b, &expected));
  ASSERT_OK(CreateFile(env_.get(), JoinPathSegments(subdir_b, file_one), &expected));
  ASSERT_OK(CreateFile(env_.get(), JoinPathSegments(subdir_b, file_two), &expected));
  ASSERT_OK(CreateDir(env_.get(), subdir_c, &expected));
  ASSERT_OK(CreateFile(env_.get(), JoinPathSegments(subdir_c, file_one), &expected));
  ASSERT_OK(CreateFile(env_.get(), JoinPathSegments(subdir_c, file_two), &expected));

  // Do the walk.
  //
  // Sadly, tr1/unordered_set doesn't implement equality operators, so we
  // compare sorted vectors instead.
  vector<string> actual;
  ASSERT_OK(env_->Walk(root, Env::PRE_ORDER, std::bind(&TestWalkCb, &actual, _1, _2, _3)));
  sort(expected.begin(), expected.end());
  sort(actual.begin(), actual.end());
  ASSERT_EQ(expected, actual);
}

static Status TestWalkErrorCb(int* num_calls,
                              Env::FileType type,
                              const string& dirname, const string& basename) {
  (*num_calls)++;
  return STATUS(Aborted, "Returning abort status");
}

TEST_F(TestEnv, TestWalkCbReturnsError) {
  string new_dir = GetTestPath("foo");
  string new_file = "myfile";
  ASSERT_OK(env_->CreateDir(new_dir));
  std::unique_ptr<WritableFile> writer;
  ASSERT_OK(env_->NewWritableFile(JoinPathSegments(new_dir, new_file), &writer));
  int num_calls = 0;
  ASSERT_TRUE(env_->Walk(new_dir, Env::PRE_ORDER,
                         std::bind(&TestWalkErrorCb, &num_calls, _1, _2, _3)).IsIOError());

  // Once for the directory and once for the file inside it.
  ASSERT_EQ(2, num_calls);
}

TEST_F(TestEnv, TestGetBlockSize) {
  // Does not exist.
  auto result = env_->GetBlockSize("does_not_exist");
  ASSERT_TRUE(!result.ok() && result.status().IsNotFound());

  // Try with a directory.
  auto block_size = ASSERT_RESULT(env_->GetBlockSize("."));
  ASSERT_GT(block_size, 0);

  // Try with a file.
  string path = GetTestPath("foo");
  std::unique_ptr<WritableFile> writer;
  ASSERT_OK(env_->NewWritableFile(path, &writer));
  block_size = ASSERT_RESULT(env_->GetBlockSize(path));
  ASSERT_GT(block_size, 0);
}

TEST_F(TestEnv, TestRWFile) {
  // Create the file.
  std::unique_ptr<RWFile> file;
  ASSERT_OK(env_->NewRWFile(GetTestPath("foo"), &file));

  // Append to it.
  string kTestData = "abcde";
  ASSERT_OK(file->Write(0, kTestData));

  // Read from it.
  Slice result;
  std::unique_ptr<uint8_t[]> scratch(new uint8_t[kTestData.length()]);
  ASSERT_OK(file->Read(0, kTestData.length(), &result, scratch.get()));
  ASSERT_EQ(result, kTestData);
  uint64_t sz;
  ASSERT_OK(file->Size(&sz));
  ASSERT_EQ(kTestData.length(), sz);

  // Write past the end of the file and rewrite some of the interior.
  ASSERT_OK(file->Write(kTestData.length() * 2, kTestData));
  ASSERT_OK(file->Write(kTestData.length(), kTestData));
  ASSERT_OK(file->Write(1, kTestData));
  string kNewTestData = "aabcdebcdeabcde";
  std::unique_ptr<uint8_t[]> scratch2(new uint8_t[kNewTestData.length()]);
  ASSERT_OK(file->Read(0, kNewTestData.length(), &result, scratch2.get()));

  // Retest.
  ASSERT_EQ(result, kNewTestData);
  ASSERT_OK(file->Size(&sz));
  ASSERT_EQ(kNewTestData.length(), sz);

  // Make sure we can't overwrite it.
  RWFileOptions opts;
  opts.mode = Env::CREATE_NON_EXISTING;
  ASSERT_TRUE(env_->NewRWFile(opts, GetTestPath("foo"), &file).IsAlreadyPresent());

  // Reopen it without truncating the existing data.
  opts.mode = Env::OPEN_EXISTING;
  ASSERT_OK(env_->NewRWFile(opts, GetTestPath("foo"), &file));
  ASSERT_OK(file->Read(0, kNewTestData.length(), &result, scratch2.get()));
  ASSERT_EQ(result, kNewTestData);
}

TEST_F(TestEnv, NonblockWritableFile) {
  string path = GetTestPath("test_nonblock");
  mkfifo(path.c_str(), 0644);

  WritableFileOptions opts;
  opts.mode = Env::CREATE_NONBLOCK_IF_NON_EXISTING;
  std::shared_ptr<WritableFile> writer;
  ASSERT_OK(env_util::OpenFileForWrite(opts, env_.get(), path, &writer));

  TestThreadHolder threads;

  auto read_data = [this, &path] {
    std::shared_ptr<SequentialFile> reader;
    ASSERT_OK(env_util::OpenFileForSequential(env_.get(), path, &reader));
    Slice s;
    std::vector<uint8_t> scratch(kOneMb);
    ASSERT_OK(reader->Read(kOneMb, &s, scratch.data()));
    ASSERT_EQ(s.size(), kOneMb);
  };

  threads.AddThreadFunctor(read_data);

  // Write 1 MB
  uint8_t scratch[kOneMb] = {};
  Slice slice(scratch, kOneMb);
  ASSERT_OK(writer->Append(slice));
}

TEST_F(TestEnv, TestCanonicalize) {
  vector<string> synonyms = { GetTestPath("."), GetTestPath("./."), GetTestPath(".//./") };
  for (const string& synonym : synonyms) {
    string result;
    ASSERT_OK(env_->Canonicalize(synonym, &result));
    ASSERT_EQ(GetTestDataDirectory(), result);
  }

  string dir = GetTestPath("some_dir");
  ASSERT_OK(env_->CreateDir(dir));
  string result;
  ASSERT_OK(env_->Canonicalize(dir + "/", &result));
  ASSERT_EQ(dir, result);

  ASSERT_TRUE(env_->Canonicalize(dir + "/bar", nullptr).IsNotFound());
}

TEST_F(TestEnv, TestGetTotalRAMBytes) {
  int64_t ram = 0;
  ASSERT_OK(env_->GetTotalRAMBytes(&ram));

  // Can't test much about it.
  ASSERT_GT(ram, 0);
}

TEST_F(TestEnv, TestGetFreeSpace) {
  char cwd[1024];
  char* ret = getcwd(cwd, sizeof(cwd));
  ASSERT_NE(ret, nullptr);

  constexpr int64_t kMaxAllowedDeltaBytes = 65536;

  // Number of times the difference between the return value from GetFreeSpaceBytes and
  // the output from command 'df' should be less than kMaxAllowedDeltaBytes before we consider
  // this test has passed.
  constexpr int kCountRequired = 10;

  // Minimum block size for MacOS is 512.
  constexpr int block_size = 512;
  const string cmd = strings::Substitute(
      "(export BLOCKSIZE=$0; df $1 | tail -1 | awk '{print $$4}' | tr -d '\\n')", block_size, cwd);

  int success_count = 0;
  for (int i = 0; i < kCountRequired * 10; i++) {
    const int64_t free_space = static_cast<int64_t>(ASSERT_RESULT(env_->GetFreeSpaceBytes(cwd)));

    string df_free_space_str;
    ASSERT_TRUE(RunShellProcess(cmd, &df_free_space_str));
    const int64_t df_free_space = block_size * ASSERT_RESULT(CheckedStoll(df_free_space_str));

     // We might not get the exact same answer because disk space is being consumed and freed.
    const int64_t delta_bytes = abs(df_free_space - free_space);
    if (delta_bytes > kMaxAllowedDeltaBytes) {
      LOG(INFO) << "df returned: " << df_free_space
                << ", GetFreeSpaceBytes returned: " << free_space;
    } else {
      success_count++;
      if (success_count >= kCountRequired) {
        break;
      }
    }
  }
  ASSERT_GE(success_count, kCountRequired);
}

// Test that CopyFile() copies all the bytes properly.
TEST_F(TestEnv, TestCopyFile) {
  string orig_path = GetTestPath("test");
  string copy_path = orig_path + ".copy";
  const int kFileSize = 1024 * 1024 + 11; // Some odd number of bytes.

  Env* env = Env::Default();
  ASSERT_NO_FATALS(WriteTestFile(env, orig_path, kFileSize));
  ASSERT_OK(env_util::CopyFile(env, orig_path, copy_path, WritableFileOptions()));
  std::unique_ptr<RandomAccessFile> copy;
  ASSERT_OK(env->NewRandomAccessFile(copy_path, &copy));
  ASSERT_NO_FATALS(ReadAndVerifyTestData(copy.get(), 0, kFileSize));
}

INSTANTIATE_TEST_CASE_P(BufferedIO, TestEnv, ::testing::Values(false));
INSTANTIATE_TEST_CASE_P(DirectIO, TestEnv, ::testing::Values(true));

}  // namespace yb
