// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
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
// Modified for yb:
// - use gtest

#include <memory>
#include <unordered_set>
#include <vector>

#include "yb/util/logging.h"
#include <gtest/gtest.h>

#include "yb/gutil/map-util.h"

#include "yb/util/env.h"
#include "yb/util/env_util.h"
#include "yb/util/memenv/memenv.h"
#include "yb/util/result.h"
#include "yb/util/test_macros.h"

using std::shared_ptr;
using std::string;
using std::unordered_set;
using std::vector;

namespace yb {

class MemEnvTest : public ::testing::Test {
 public:
  Env* env_;

  MemEnvTest()
      : env_(NewMemEnv(Env::Default())) {
  }
  ~MemEnvTest() {
    delete env_;
  }
};

TEST_F(MemEnvTest, Basics) {
  uint64_t file_size;
  std::unique_ptr<WritableFile> writable_file;
  vector<string> children;

  // Create the directory.
  ASSERT_FALSE(env_->FileExists("/dir"));
  ASSERT_OK(env_->CreateDir("/dir"));
  ASSERT_TRUE(env_->FileExists("/dir"));

  // Check that the directory is empty.
  ASSERT_TRUE(!env_->FileExists("/dir/non_existent"));
  ASSERT_FALSE(env_->GetFileSize("/dir/non_existent").ok());
  ASSERT_OK(env_->GetChildren("/dir", &children));
  ASSERT_EQ(0, children.size());

  // Create a file.
  ASSERT_OK(env_->NewWritableFile("/dir/f", &writable_file));
  writable_file.reset();

  // Check that the file exists.
  ASSERT_TRUE(env_->FileExists("/dir/f"));
  file_size = ASSERT_RESULT(env_->GetFileSize("/dir/f"));
  ASSERT_EQ(0, file_size);
  ASSERT_OK(env_->GetChildren("/dir", &children));
  ASSERT_EQ(1, children.size());
  ASSERT_EQ("f", children[0]);

  // Write to the file.
  ASSERT_OK(env_->NewWritableFile("/dir/f", &writable_file));
  ASSERT_OK(writable_file->Append("abc"));
  writable_file.reset();

  // Check for expected size.
  file_size = ASSERT_RESULT(env_->GetFileSize("/dir/f"));
  ASSERT_EQ(3, file_size);

  // Check that renaming works.
  ASSERT_TRUE(!env_->RenameFile("/dir/non_existent", "/dir/g").ok());
  ASSERT_OK(env_->RenameFile("/dir/f", "/dir/g"));
  ASSERT_TRUE(!env_->FileExists("/dir/f"));
  ASSERT_TRUE(env_->FileExists("/dir/g"));
  file_size = ASSERT_RESULT(env_->GetFileSize("/dir/g"));
  ASSERT_EQ(3, file_size);

  // Check that opening non-existent file fails.
  std::unique_ptr<SequentialFile> seq_file;
  std::unique_ptr<RandomAccessFile> rand_file;
  ASSERT_TRUE(!env_->NewSequentialFile("/dir/non_existent", &seq_file).ok());
  ASSERT_TRUE(!seq_file);
  ASSERT_TRUE(!env_->NewRandomAccessFile("/dir/non_existent", &rand_file).ok());
  ASSERT_TRUE(!rand_file);

  // Check that deleting works.
  ASSERT_TRUE(!env_->DeleteFile("/dir/non_existent").ok());
  ASSERT_OK(env_->DeleteFile("/dir/g"));
  ASSERT_TRUE(!env_->FileExists("/dir/g"));
  ASSERT_OK(env_->GetChildren("/dir", &children));
  ASSERT_EQ(0, children.size());
  ASSERT_OK(env_->DeleteDir("/dir"));
  ASSERT_FALSE(env_->FileExists("/dir"));
}

TEST_F(MemEnvTest, ReadWrite) {
  Slice result;
  uint8_t scratch[100];

  ASSERT_OK(env_->CreateDir("/dir"));

  {
    std::unique_ptr<WritableFile> writable_file;
    ASSERT_OK(env_->NewWritableFile("/dir/f", &writable_file));
    ASSERT_OK(writable_file->Append("hello "));
    ASSERT_OK(writable_file->Append("world"));
  }

  {
    // Read sequentially.
    std::unique_ptr<SequentialFile> seq_file;
    ASSERT_OK(env_->NewSequentialFile("/dir/f", &seq_file));
    ASSERT_OK(seq_file->Read(5, &result, scratch)); // Read "hello".
    ASSERT_EQ(0, result.compare("hello"));
    ASSERT_OK(seq_file->Skip(1));
    ASSERT_OK(seq_file->Read(1000, &result, scratch)); // Read "world".
    ASSERT_EQ(0, result.compare("world"));
    ASSERT_OK(seq_file->Read(1000, &result, scratch)); // Try reading past EOF.
    ASSERT_EQ(0, result.size());
    ASSERT_OK(seq_file->Skip(100)); // Try to skip past end of file.
    ASSERT_OK(seq_file->Read(1000, &result, scratch));
    ASSERT_EQ(0, result.size());
  }

  {
    // Random reads.
    std::unique_ptr<RandomAccessFile> rand_file;
    ASSERT_OK(env_->NewRandomAccessFile("/dir/f", &rand_file));
    ASSERT_OK(rand_file->Read(6, 5, &result, scratch)); // Read "world".
    ASSERT_EQ(0, result.compare("world"));
    ASSERT_OK(rand_file->Read(0, 5, &result, scratch)); // Read "hello".
    ASSERT_EQ(0, result.compare("hello"));
    ASSERT_OK(rand_file->Read(10, 100, &result, scratch)); // Read "d".
    ASSERT_EQ(0, result.compare("d"));

    // Too high offset.
    ASSERT_TRUE(!rand_file->Read(1000, 5, &result, scratch).ok());
  }
}

TEST_F(MemEnvTest, Locks) {
  FileLock* lock;

  // These are no-ops, but we test they return success.
  ASSERT_OK(env_->LockFile("some file", &lock, false /* recursive_lock_ok */));
  ASSERT_OK(env_->UnlockFile(lock));
}

TEST_F(MemEnvTest, Misc) {
  string test_dir;
  ASSERT_OK(env_->GetTestDirectory(&test_dir));
  ASSERT_TRUE(!test_dir.empty());

  std::unique_ptr<WritableFile> writable_file;
  ASSERT_OK(env_->NewWritableFile("/a/b", &writable_file));

  // These are no-ops, but we test they return success.
  ASSERT_OK(writable_file->Sync());
  ASSERT_OK(writable_file->Flush(WritableFile::FLUSH_SYNC));
  ASSERT_OK(writable_file->Flush(WritableFile::FLUSH_ASYNC));
  ASSERT_OK(writable_file->Close());
}

TEST_F(MemEnvTest, LargeWrite) {
  const size_t kWriteSize = 300 * 1024;
  std::unique_ptr<uint8_t[]> scratch(new uint8_t[kWriteSize * 2]);

  string write_data;
  for (size_t i = 0; i < kWriteSize; ++i) {
    write_data.append(1, static_cast<char>(i));
  }

  std::unique_ptr<WritableFile> writable_file;
  ASSERT_OK(env_->NewWritableFile("/dir/f", &writable_file));
  ASSERT_OK(writable_file->Append("foo"));
  ASSERT_OK(writable_file->Append(write_data));
  writable_file.reset();

  std::unique_ptr<SequentialFile> seq_file;
  Slice result;
  ASSERT_OK(env_->NewSequentialFile("/dir/f", &seq_file));
  ASSERT_OK(seq_file->Read(3, &result, scratch.get())); // Read "foo".
  ASSERT_EQ(0, result.compare("foo"));

  size_t read = 0;
  string read_data;
  while (read < kWriteSize) {
    ASSERT_OK(seq_file->Read(kWriteSize - read, &result, scratch.get()));
    read_data.append(reinterpret_cast<const char *>(result.data()),
                     result.size());
    read += result.size();
  }
  ASSERT_TRUE(write_data == read_data);
}

TEST_F(MemEnvTest, Overwrite) {
  // File does not exist, create it.
  shared_ptr<WritableFile> writer;
  ASSERT_OK(env_util::OpenFileForWrite(env_, "some file", &writer));

  // File exists, overwrite it.
  ASSERT_OK(env_util::OpenFileForWrite(env_, "some file", &writer));

  // File exists, try to overwrite (and fail).
  WritableFileOptions opts;
  opts.mode = Env::CREATE_NON_EXISTING;
  Status s = env_util::OpenFileForWrite(opts,
                                        env_, "some file", &writer);
  ASSERT_TRUE(s.IsAlreadyPresent());
}

TEST_F(MemEnvTest, Reopen) {
  string first = "The quick brown fox";
  string second = "jumps over the lazy dog";

  // Create the file and write to it.
  shared_ptr<WritableFile> writer;
  ASSERT_OK(env_util::OpenFileForWrite(env_, "some file", &writer));
  ASSERT_OK(writer->Append(first));
  ASSERT_EQ(first.length(), writer->Size());
  ASSERT_OK(writer->Close());

  // Reopen it and append to it.
  WritableFileOptions reopen_opts;
  reopen_opts.mode = Env::OPEN_EXISTING;
  ASSERT_OK(env_util::OpenFileForWrite(reopen_opts,
                                       env_, "some file", &writer));
  ASSERT_EQ(first.length(), writer->Size());
  ASSERT_OK(writer->Append(second));
  ASSERT_EQ(first.length() + second.length(), writer->Size());
  ASSERT_OK(writer->Close());

  // Check that the file has both strings.
  shared_ptr<RandomAccessFile> reader;
  ASSERT_OK(env_util::OpenFileForRandom(env_, "some file", &reader));
  uint64_t size = ASSERT_RESULT(reader->Size());
  ASSERT_EQ(first.length() + second.length(), size);
  Slice s;
  std::vector<uint8_t> scratch(size);
  ASSERT_OK(env_util::ReadFully(reader.get(), 0, size, &s, scratch.data()));
  ASSERT_EQ(first + second, s.ToString());
}

TEST_F(MemEnvTest, TempFile) {
  string tmpl = "foo.XXXXXX";
  string bad_tmpl = "foo.YYY";

  string path;
  std::unique_ptr<WritableFile> file;

  // Ensure we don't accept a bad template.
  Status s = env_->NewTempWritableFile(WritableFileOptions(), bad_tmpl, &path, &file);
  ASSERT_TRUE(s.IsInvalidArgument()) << "Should not accept bad template: " << s.ToString();
  ASSERT_STR_CONTAINS(s.ToString(), "must end with the string XXXXXX");

  // Create multiple temp files, ensure no collisions.
  unordered_set<string> paths;
  for (int i = 0; i < 10; i++) {
    ASSERT_OK(env_->NewTempWritableFile(WritableFileOptions(), tmpl, &path, &file));
    VLOG(1) << "Created temporary file at path " << path;
    ASSERT_EQ(path.length(), tmpl.length()) << "Template and final path should have same length";
    ASSERT_NE(path, tmpl) << "Template and final path should differ";
    ASSERT_OK(file->Append("Hello, tempfile.\n"));
    ASSERT_OK(file->Close());
    ASSERT_FALSE(ContainsKey(paths, path)) << "Created " << path << " twice!";
    InsertOrDie(&paths, path); // Will crash if we have a duplicate.
  }

  // Delete the files we created.
  for (const string& p : paths) {
    ASSERT_OK(env_->DeleteFile(p));
  }
}

TEST_F(MemEnvTest, TestRWFile) {
  // Create the file.
  std::unique_ptr<RWFile> file;
  ASSERT_OK(env_->NewRWFile("foo", &file));

  // Append to it.
  string kTestData = "abcdefghijklmno";
  ASSERT_OK(file->Write(0, kTestData));

  // Read from it.
  Slice result;
  std::unique_ptr<uint8_t[]> scratch(new uint8_t[kTestData.length()]);
  ASSERT_OK(file->Read(0, kTestData.length(), &result, scratch.get()));
  ASSERT_EQ(result, kTestData);

  // Try to rewrite; it shouldn't work.
  ASSERT_TRUE(file->Write(0, kTestData).IsNotSupported());

  // Make sure we can't overwrite it.
  RWFileOptions opts;
  opts.mode = Env::CREATE_NON_EXISTING;
  ASSERT_TRUE(env_->NewRWFile(opts, "foo", &file).IsAlreadyPresent());

  // Reopen it without truncating the existing data.
  opts.mode = Env::OPEN_EXISTING;
  ASSERT_OK(env_->NewRWFile(opts, "foo", &file));
  ASSERT_OK(file->Read(0, kTestData.length(), &result, scratch.get()));
  ASSERT_EQ(result, kTestData);
}

}  // namespace yb
