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

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <google/protobuf/descriptor.pb.h>
#include <gtest/gtest.h>

#include "yb/util/env_util.h"
#include "yb/util/memenv/memenv.h"
#include "yb/util/pb_util-internal.h"
#include "yb/util/pb_util.h"
#include "yb/util/proto_container_test.pb.h"
#include "yb/util/proto_container_test2.pb.h"
#include "yb/util/proto_container_test3.pb.h"
#include "yb/util/protobuf_util.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace yb {
namespace pb_util {

using google::protobuf::FileDescriptorSet;
using internal::WritableFileOutputStream;
using std::ostringstream;
using std::shared_ptr;
using std::string;
using std::vector;

static const char* kTestFileName = "pb_container.meta";
static const char* kTestKeyvalName = "my-key";
static const int kTestKeyvalValue = 1;
static const std::string kTestString = "test-string";

class TestPBUtil : public YBTest {
 public:
  void SetUp() override {
    YBTest::SetUp();
    path_ = GetTestPath(kTestFileName);
  }

 protected:
  // Create a container file with expected values.
  // Since this is a unit test class, and we want it to be fast, we do not
  // fsync by default.
  Status CreateKnownGoodContainerFile(CreateMode create = OVERWRITE,
                                      SyncMode sync = NO_SYNC);

  // XORs the data in the specified range of the file at the given path.
  Status BitFlipFileByteRange(const string& path, uint64_t offset, uint64_t length);

  void DumpPBCToString(const string& path, bool oneline_output, string* ret);

  // Output file name for most unit tests.
  string path_;
};

Status TestPBUtil::CreateKnownGoodContainerFile(CreateMode create, SyncMode sync) {
  ProtoContainerTestPB test_pb;
  test_pb.set_name(kTestKeyvalName);
  test_pb.set_value(kTestKeyvalValue);
  return WritePBContainerToPath(env_.get(), path_, test_pb, create, sync);
}

Status TestPBUtil::BitFlipFileByteRange(const string& path, uint64_t offset, uint64_t length) {
  faststring buf;
  // Read the data from disk.
  {
    std::unique_ptr<RandomAccessFile> file;
    RETURN_NOT_OK(env_->NewRandomAccessFile(path, &file));
    uint64_t size = VERIFY_RESULT(file->Size());
    Slice slice;
    faststring scratch;
    scratch.resize(size);
    RETURN_NOT_OK(env_util::ReadFully(file.get(), 0, size, &slice, scratch.data()));
    buf.append(slice.data(), slice.size());
  }

  // Flip the bits.
  for (uint64_t i = 0; i < length; i++) {
    uint8_t* addr = buf.data() + offset + i;
    *addr = ~*addr;
  }

  // Write the data back to disk.
  std::unique_ptr<WritableFile> file;
  RETURN_NOT_OK(env_->NewWritableFile(path, &file));
  RETURN_NOT_OK(file->Append(buf));
  RETURN_NOT_OK(file->Close());

  return Status::OK();
}

TEST_F(TestPBUtil, TestWritableFileOutputStream) {
  std::unique_ptr<Env> env(NewMemEnv(Env::Default()));
  shared_ptr<WritableFile> file;
  ASSERT_OK(env_util::OpenFileForWrite(env.get(), "/test", &file));

  WritableFileOutputStream stream(file.get(), 4096);

  void* buf;
  int size;

  // First call should yield the whole buffer.
  ASSERT_TRUE(stream.Next(&buf, &size));
  ASSERT_EQ(4096, size);
  ASSERT_EQ(4096, stream.ByteCount());

  // Backup 1000 and the next call should yield 1000
  stream.BackUp(1000);
  ASSERT_EQ(3096, stream.ByteCount());

  ASSERT_TRUE(stream.Next(&buf, &size));
  ASSERT_EQ(1000, size);

  // Another call should flush and yield a new buffer of 4096
  ASSERT_TRUE(stream.Next(&buf, &size));
  ASSERT_EQ(4096, size);
  ASSERT_EQ(8192, stream.ByteCount());

  // Should be able to backup to 7192
  stream.BackUp(1000);
  ASSERT_EQ(7192, stream.ByteCount());

  // Flushing shouldn't change written count.
  ASSERT_TRUE(stream.Flush());
  ASSERT_EQ(7192, stream.ByteCount());

  // Since we just flushed, we should get another full buffer.
  ASSERT_TRUE(stream.Next(&buf, &size));
  ASSERT_EQ(4096, size);
  ASSERT_EQ(7192 + 4096, stream.ByteCount());

  ASSERT_TRUE(stream.Flush());

  ASSERT_EQ(stream.ByteCount(), file->Size());
}

// Basic read/write test.
TEST_F(TestPBUtil, TestPBContainerSimple) {
  // Exercise both the SYNC and NO_SYNC codepaths, despite the fact that we
  // aren't able to observe a difference in the test.
  vector<SyncMode> modes = { SYNC, NO_SYNC };
  for (SyncMode mode : modes) {

    // Write the file.
    ASSERT_OK(CreateKnownGoodContainerFile(NO_OVERWRITE, mode));

    // Read it back, should validate and contain the expected values.
    ProtoContainerTestPB test_pb;
    ASSERT_OK(ReadPBContainerFromPath(env_.get(), path_, &test_pb));
    ASSERT_EQ(kTestKeyvalName, test_pb.name());
    ASSERT_EQ(kTestKeyvalValue, test_pb.value());

    // Delete the file.
    ASSERT_OK(env_->DeleteFile(path_));
  }
}

// Corruption / various failure mode test.
TEST_F(TestPBUtil, TestPBContainerCorruption) {
  // Test that we indicate when the file does not exist.
  ProtoContainerTestPB test_pb;
  Status s = ReadPBContainerFromPath(env_.get(), path_, &test_pb);
  ASSERT_TRUE(s.IsNotFound()) << "Should not be found: " << path_ << ": " << s.ToString();

  // Test that an empty file looks like corruption.
  {
    // Create the empty file.
    std::unique_ptr<WritableFile> file;
    ASSERT_OK(env_->NewWritableFile(path_, &file));
    ASSERT_OK(file->Close());
  }
  s = ReadPBContainerFromPath(env_.get(), path_, &test_pb);
  ASSERT_TRUE(s.IsCorruption()) << "Should be zero length: " << path_ << ": " << s.ToString();
  ASSERT_STR_CONTAINS(s.ToString(), "File size not large enough to be valid");

  // Test truncated file.
  ASSERT_OK(CreateKnownGoodContainerFile());
  uint64_t known_good_size = ASSERT_RESULT(env_->GetFileSize(path_));
  int ret = truncate(path_.c_str(), known_good_size - 2);
  if (ret != 0) {
    PLOG(ERROR) << "truncate() of file " << path_ << " failed";
    FAIL();
  }
  s = ReadPBContainerFromPath(env_.get(), path_, &test_pb);
  ASSERT_TRUE(s.IsCorruption()) << "Should be incorrect size: " << path_ << ": " << s.ToString();
  ASSERT_STR_CONTAINS(s.ToString(), "File size not large enough to be valid");

  // Test corrupted magic.
  ASSERT_OK(CreateKnownGoodContainerFile());
  ASSERT_OK(BitFlipFileByteRange(path_, 0, 2));
  s = ReadPBContainerFromPath(env_.get(), path_, &test_pb);
  ASSERT_TRUE(s.IsCorruption()) << "Should have invalid magic: " << path_ << ": " << s.ToString();
  ASSERT_STR_CONTAINS(s.ToString(), "Invalid magic number");

  // Test corrupted version.
  ASSERT_OK(CreateKnownGoodContainerFile());
  ASSERT_OK(BitFlipFileByteRange(path_, 8, 2));
  s = ReadPBContainerFromPath(env_.get(), path_, &test_pb);
  ASSERT_TRUE(s.IsNotSupported()) << "Should have unsupported version number: " << path_ << ": "
                                  << s.ToString();
  ASSERT_STR_CONTAINS(s.ToString(), "we only support version 1");

  // Test corrupted size.
  ASSERT_OK(CreateKnownGoodContainerFile());
  ASSERT_OK(BitFlipFileByteRange(path_, 12, 2));
  s = ReadPBContainerFromPath(env_.get(), path_, &test_pb);
  ASSERT_TRUE(s.IsCorruption()) << "Should be incorrect size: " << path_ << ": " << s.ToString();
  ASSERT_STR_CONTAINS(s.ToString(), "File size not large enough to be valid");

  // Test corrupted data (looks like bad checksum).
  ASSERT_OK(CreateKnownGoodContainerFile());
  ASSERT_OK(BitFlipFileByteRange(path_, 16, 2));
  s = ReadPBContainerFromPath(env_.get(), path_, &test_pb);
  ASSERT_TRUE(s.IsCorruption()) << "Should be incorrect checksum: " << path_ << ": "
                                << s.ToString();
  ASSERT_STR_CONTAINS(s.ToString(), "Incorrect checksum");

  // Test corrupted checksum.
  ASSERT_OK(CreateKnownGoodContainerFile());
  ASSERT_OK(BitFlipFileByteRange(path_, known_good_size - 4, 2));
  s = ReadPBContainerFromPath(env_.get(), path_, &test_pb);
  ASSERT_TRUE(s.IsCorruption()) << "Should be incorrect checksum: " << path_ << ": "
                                << s.ToString();
  ASSERT_STR_CONTAINS(s.ToString(), "Incorrect checksum");
}

TEST_F(TestPBUtil, TestMultipleMessages) {
  ProtoContainerTestPB pb;
  pb.set_name("foo");
  pb.set_note("bar");

  std::unique_ptr<WritableFile> writer;
  ASSERT_OK(env_->NewWritableFile(path_, &writer));
  WritablePBContainerFile pb_writer(std::move(writer));
  ASSERT_OK(pb_writer.Init(pb));

  for (int i = 0; i < 10; i++) {
    pb.set_value(i);
    ASSERT_OK(pb_writer.Append(pb));
  }
  ASSERT_OK(pb_writer.Close());

  int pbs_read = 0;
  std::unique_ptr<RandomAccessFile> reader;
  ASSERT_OK(env_->NewRandomAccessFile(path_, &reader));
  ReadablePBContainerFile pb_reader(std::move(reader));
  ASSERT_OK(pb_reader.Init());
  for (int i = 0;; i++) {
    ProtoContainerTestPB read_pb;
    Status s = pb_reader.ReadNextPB(&read_pb);
    if (s.IsEndOfFile()) {
      break;
    }
    ASSERT_OK(s);
    ASSERT_EQ(pb.name(), read_pb.name());
    ASSERT_EQ(read_pb.value(), i);
    ASSERT_EQ(pb.note(), read_pb.note());
    pbs_read++;
  }
  ASSERT_EQ(10, pbs_read);
  ASSERT_OK(pb_reader.Close());
}

TEST_F(TestPBUtil, TestInterleavedReadWrite) {
  ProtoContainerTestPB pb;
  pb.set_name("foo");
  pb.set_note("bar");

  // Open the file for writing and reading.
  std::unique_ptr<WritableFile> writer;
  ASSERT_OK(env_->NewWritableFile(path_, &writer));
  WritablePBContainerFile pb_writer(std::move(writer));
  std::unique_ptr<RandomAccessFile> reader;
  ASSERT_OK(env_->NewRandomAccessFile(path_, &reader));
  ReadablePBContainerFile pb_reader(std::move(reader));

  // Write the header (writer) and validate it (reader).
  ASSERT_OK(pb_writer.Init(pb));
  ASSERT_OK(pb_reader.Init());

  for (int i = 0; i < 10; i++) {
    // Write a message and read it back.
    pb.set_value(i);
    ASSERT_OK(pb_writer.Append(pb));
    ProtoContainerTestPB read_pb;
    ASSERT_OK(pb_reader.ReadNextPB(&read_pb));
    ASSERT_EQ(pb.name(), read_pb.name());
    ASSERT_EQ(read_pb.value(), i);
    ASSERT_EQ(pb.note(), read_pb.note());
  }

  // After closing the writer, the reader should be out of data.
  ASSERT_OK(pb_writer.Close());
  ASSERT_TRUE(pb_reader.ReadNextPB(nullptr).IsEndOfFile());
  ASSERT_OK(pb_reader.Close());
}

TEST_F(TestPBUtil, TestPopulateDescriptorSet) {
  {
    // No dependencies --> just one proto.
    ProtoContainerTestPB pb;
    FileDescriptorSet protos;
    WritablePBContainerFile::PopulateDescriptorSet(
        pb.GetDescriptor()->file(), &protos);
    ASSERT_EQ(1, protos.file_size());
  }
  {
    // One direct dependency --> two protos.
    ProtoContainerTest2PB pb;
    FileDescriptorSet protos;
    WritablePBContainerFile::PopulateDescriptorSet(
        pb.GetDescriptor()->file(), &protos);
    ASSERT_EQ(2, protos.file_size());
  }
  {
    // One direct and one indirect dependency --> three protos.
    ProtoContainerTest3PB pb;
    FileDescriptorSet protos;
    WritablePBContainerFile::PopulateDescriptorSet(
        pb.GetDescriptor()->file(), &protos);
    ASSERT_EQ(3, protos.file_size());
  }
}

void TestPBUtil::DumpPBCToString(const string& path, bool oneline_output,
                                 string* ret) {
  std::unique_ptr<RandomAccessFile> reader;
  ASSERT_OK(env_->NewRandomAccessFile(path, &reader));
  ReadablePBContainerFile pb_reader(std::move(reader));
  ASSERT_OK(pb_reader.Init());
  ostringstream oss;
  ASSERT_OK(pb_reader.Dump(&oss, oneline_output));
  ASSERT_OK(pb_reader.Close());
  *ret = oss.str();
}

TEST_F(TestPBUtil, TestDumpPBContainer) {
  const char* kExpectedOutput =
      "yb.ProtoContainerTest3PB 0\n"
      "-------\n"
      "record_one {\n"
      "  name: \"foo\"\n"
      "  value: 0\n"
      "}\n"
      "record_two {\n"
      "  record {\n"
      "    name: \"foo\"\n"
      "    value: 0\n"
      "  }\n"
      "}\n"
      "\n"
      "yb.ProtoContainerTest3PB 1\n"
      "-------\n"
      "record_one {\n"
      "  name: \"foo\"\n"
      "  value: 1\n"
      "}\n"
      "record_two {\n"
      "  record {\n"
      "    name: \"foo\"\n"
      "    value: 2\n"
      "  }\n"
      "}\n\n";

  const char* kExpectedOutputShort =
    "0\trecord_one { name: \"foo\" value: 0 } record_two { record { name: \"foo\" value: 0 } }\n"
    "1\trecord_one { name: \"foo\" value: 1 } record_two { record { name: \"foo\" value: 2 } }\n";

  ProtoContainerTest3PB pb;
  pb.mutable_record_one()->set_name("foo");
  pb.mutable_record_two()->mutable_record()->set_name("foo");

  std::unique_ptr<WritableFile> writer;
  ASSERT_OK(env_->NewWritableFile(path_, &writer));
  WritablePBContainerFile pb_writer(std::move(writer));
  ASSERT_OK(pb_writer.Init(pb));

  for (int i = 0; i < 2; i++) {
    pb.mutable_record_one()->set_value(i);
    pb.mutable_record_two()->mutable_record()->set_value(i*2);
    ASSERT_OK(pb_writer.Append(pb));
  }
  ASSERT_OK(pb_writer.Close());

  string output;
  DumpPBCToString(path_, false, &output);
  ASSERT_STREQ(kExpectedOutput, output.c_str());

  DumpPBCToString(path_, true, &output);
  ASSERT_STREQ(kExpectedOutputShort, output.c_str());
}

TEST_F(TestPBUtil, TestOverwriteExistingPB) {
  ASSERT_OK(CreateKnownGoodContainerFile(NO_OVERWRITE));
  ASSERT_TRUE(CreateKnownGoodContainerFile(NO_OVERWRITE).IsAlreadyPresent());
  ASSERT_OK(CreateKnownGoodContainerFile(OVERWRITE));
  ASSERT_OK(CreateKnownGoodContainerFile(OVERWRITE));
}

PB_ENUM_FORMATTERS(TestPBEnum)

TEST_F(TestPBUtil, TestEnumToString) {
  {
    std::stringstream ss;
    ss << TestPBEnum::FOO;
    ASSERT_EQ("FOO", ss.str());
    ASSERT_EQ("FOO", PBEnumToString(TestPBEnum::FOO));
    ASSERT_EQ("FOO", ToString(TestPBEnum::FOO));
  }

#if !defined(ADDRESS_SANITIZER)
  {
    std::stringstream ss;
    const auto kInvalidValue = static_cast<TestPBEnum>(10);
    ss << kInvalidValue;
    const char* kExpectedStr = "<unknown TestPBEnum : 10>";
    ASSERT_EQ(kExpectedStr, ss.str());
    ASSERT_EQ(kExpectedStr, PBEnumToString(kInvalidValue));
    ASSERT_EQ(kExpectedStr, ToString(kInvalidValue));
  }
#endif
}

TEST_F(TestPBUtil, TestPBRequiredToRepeated) {
  // Write the file with required fields.
  {
    TestObjectRequiredPB pb;
    pb.set_string1(kTestString + "1");
    pb.set_string2(kTestString + "2");
    pb.mutable_record()->set_text(kTestString);
    ASSERT_OK(WritePBContainerToPath(env_.get(), path_, pb, OVERWRITE, SYNC));
  }

  // Read it back as repeated fields, should validate and contain the expected values.
  TestObjectRepeatedPB pb;
  ASSERT_OK(ReadPBContainerFromPath(env_.get(), path_, &pb));
  ASSERT_EQ(1, pb.string1_size());
  ASSERT_EQ(1, pb.string2_size());
  ASSERT_EQ(1, pb.record_size());
  ASSERT_EQ(kTestString + "1", pb.string1()[0]);
  ASSERT_EQ(kTestString + "2", pb.string2()[0]);
  ASSERT_EQ(kTestString, pb.record()[0].text()[0]);

  // Delete the file.
  ASSERT_OK(env_->DeleteFile(path_));
}

TEST_F(TestPBUtil, TestPBRequiredToOptional) {
  // Write the file with required fields.
  {
    TestObjectRequiredPB pb;
    pb.set_string1(kTestString + "1");
    pb.set_string2(kTestString + "2");
    pb.mutable_record()->set_text(kTestString);
    ASSERT_OK(WritePBContainerToPath(env_.get(), path_, pb, OVERWRITE, SYNC));
  }

  // Read it back as optional fields, should validate and contain the expected values.
  TestObjectOptionalPB pb;
  ASSERT_OK(ReadPBContainerFromPath(env_.get(), path_, &pb));
  ASSERT_TRUE(pb.has_string1());
  ASSERT_TRUE(pb.has_string2());
  ASSERT_TRUE(pb.has_record());
  ASSERT_TRUE(pb.record().has_text());
  ASSERT_EQ(kTestString + "1", pb.string1());
  ASSERT_EQ(kTestString + "2", pb.string2());
  ASSERT_EQ(kTestString, pb.record().text());

  // Delete the file.
  ASSERT_OK(env_->DeleteFile(path_));
}

// Capture the outcome of the SCHECK into a variable.
#define PB_FIELDS_ARE_SET(pb, ...) \
  [&pb]() -> Status { \
    SCHECK_PB_FIELDS_SET(pb, __VA_ARGS__); \
    return Status::OK(); \
  }()

TEST_F(TestPBUtil, TestScheckPbFieldsAreSetMacro) {
  ProtoContainerTestPB pb;
  pb.set_name("foo");
  pb.set_note("bar");
  // value not set

  ASSERT_OK(PB_FIELDS_ARE_SET(pb, name));
  ASSERT_OK(PB_FIELDS_ARE_SET(pb, note));
  ASSERT_NOK_STR_CONTAINS(PB_FIELDS_ARE_SET(pb, value), "Missing required arguments: [value]");

  ASSERT_OK(PB_FIELDS_ARE_SET(pb, name, note));
  ASSERT_NOK_STR_CONTAINS(
      PB_FIELDS_ARE_SET(pb, name, value), "Missing required arguments: [value]");

  ASSERT_NOK_STR_CONTAINS(
      PB_FIELDS_ARE_SET(pb, value, note), "Missing required arguments: [value]");

  ASSERT_NOK_STR_CONTAINS(
      PB_FIELDS_ARE_SET(pb, name, value, note), "Missing required arguments: [value]");

  pb.Clear();
  pb.set_name("foo");
  // value, note not set

  ASSERT_OK(PB_FIELDS_ARE_SET(pb, name));
  ASSERT_NOK_STR_CONTAINS(
      PB_FIELDS_ARE_SET(pb, name, value, note), "Missing required arguments: [value, note]");
  ASSERT_NOK_STR_CONTAINS(
      PB_FIELDS_ARE_SET(pb, note, name, value), "Missing required arguments: [note, value]");

  pb.Clear();
  pb.set_note("foo");
  // name, value not set

  ASSERT_OK(PB_FIELDS_ARE_SET(pb, note));
  ASSERT_NOK_STR_CONTAINS(
      PB_FIELDS_ARE_SET(pb, name, value, note), "Missing required arguments: [name, value]");
}

#undef PB_FIELDS_ARE_SET

// Capture the outcome of the SCHECK into a variable.
#define PB_FIELDS_NOT_EMPTY(pb, ...) \
  [&pb]() -> Status { \
    SCHECK_PB_FIELDS_NOT_EMPTY(pb, __VA_ARGS__); \
    return Status::OK(); \
  }()

TEST_F(TestPBUtil, TestScheckPbFieldsNotEmptyMacro) {
  ProtoContainerTestPB pb;

  // Empty note.
  pb.set_note("");
  ASSERT_NOK_STR_CONTAINS(PB_FIELDS_NOT_EMPTY(pb, note), "Empty required arguments: [note]");
  ASSERT_OK(PB_FIELDS_NOT_EMPTY(pb, value));

  // Empty repeated string field.
  ASSERT_NOK_STR_CONTAINS(
      PB_FIELDS_NOT_EMPTY(pb, str_list), "Empty required arguments: [str_list]");
  pb.add_str_list("");
  ASSERT_NOK_STR_CONTAINS(
      PB_FIELDS_NOT_EMPTY(pb, str_list), "Empty required arguments: [str_list]");

  // Empty repeated int field.
  ASSERT_NOK_STR_CONTAINS(
      PB_FIELDS_NOT_EMPTY(pb, str_list, int_list),
      "Empty required arguments: [str_list, int_list]");

  pb.add_int_list(0);
  ASSERT_NOK_STR_CONTAINS(
      PB_FIELDS_NOT_EMPTY(pb, str_list, int_list), "Empty required arguments: [str_list]");

  pb.clear_str_list();
  pb.add_str_list("test");

  ASSERT_NOK_STR_CONTAINS(
      PB_FIELDS_NOT_EMPTY(pb, name, value, note, str_list, int_list),
      "Empty required arguments: [name, note]");

  pb.set_name("name");
  pb.set_note("note");
  ASSERT_OK(PB_FIELDS_NOT_EMPTY(pb, name, value, note, str_list, int_list));
}

#undef PB_FIELDS_NOT_EMPTY

} // namespace pb_util
} // namespace yb
