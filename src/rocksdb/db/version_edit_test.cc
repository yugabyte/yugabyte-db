//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/version_edit.h"

#include "util/testharness.h"
#include "util/testutil.h"

namespace rocksdb {

namespace {

void TestEncodeDecode(const VersionEdit &edit) {
  auto extractor = test::MakeBoundaryValuesExtractor();
  std::string encoded, encoded2;
  edit.EncodeTo(&encoded);
  VersionEdit parsed;
  Status s = parsed.DecodeFrom(extractor.get(), encoded);
  ASSERT_TRUE(s.ok()) << s.ToString();

  auto str = edit.DebugString();
  auto str2 = parsed.DebugString();
  ASSERT_EQ(str, str2);

  parsed.EncodeTo(&encoded2);
  ASSERT_EQ(encoded, encoded2);
}

} // namespace

class VersionEditTest : public testing::Test {};

TEST_F(VersionEditTest, EncodeDecode) {
  static const uint64_t kBig = 1ull << 50;
  static const uint32_t kBig32Bit = 1ull << 30;

  VersionEdit edit;
  for (int i = 0; i < 4; i++) {
    TestEncodeDecode(edit);
    auto smallest = MakeFileBoundaryValues("foo", kBig + 500 + i, kTypeValue);
    auto largest = MakeFileBoundaryValues("zoo", kBig + 600 + i, kTypeDeletion);
    smallest.user_values.push_back(test::MakeIntBoundaryValue(33));
    largest.user_values.push_back(test::MakeStringBoundaryValue("Hello"));
    edit.AddFile(3,
                 FileDescriptor(kBig + 300 + i, kBig32Bit + 400 + i, 0, 0),
                 smallest,
                 largest,
                 false);
    edit.DeleteFile(4, kBig + 700 + i);
  }

  edit.SetComparatorName("foo");
  edit.SetLogNumber(kBig + 100);
  edit.SetNextFile(kBig + 200);
  edit.SetLastSequence(kBig + 1000);
  TestEncodeDecode(edit);
}

TEST_F(VersionEditTest, EncodeDecodeNewFile4) {
  static const uint64_t kBig = 1ull << 50;

  VersionEdit edit;
  edit.AddFile(3,
               FileDescriptor(300, 3, 100, 30),
               MakeFileBoundaryValues("foo", kBig + 500, kTypeValue),
               MakeFileBoundaryValues("zoo", kBig + 600, kTypeDeletion),
               true);
  edit.AddFile(4,
               FileDescriptor(301, 3, 100, 30),
               MakeFileBoundaryValues("foo", kBig + 501, kTypeValue),
               MakeFileBoundaryValues("zoo", kBig + 601, kTypeDeletion),
               false);
  edit.AddFile(5,
               FileDescriptor(302, 0, 100, 30),
               MakeFileBoundaryValues("foo", kBig + 502, kTypeValue),
               MakeFileBoundaryValues("zoo", kBig + 602, kTypeDeletion),
               true);

  edit.DeleteFile(4, 700);

  edit.SetComparatorName("foo");
  edit.SetLogNumber(kBig + 100);
  edit.SetNextFile(kBig + 200);
  edit.SetLastSequence(kBig + 1000);
  TestEncodeDecode(edit);

  auto extractor = test::MakeBoundaryValuesExtractor();
  std::string encoded, encoded2;
  edit.EncodeTo(&encoded);
  VersionEdit parsed;
  Status s = parsed.DecodeFrom(extractor.get(), encoded);
  ASSERT_TRUE(s.ok()) << s.ToString();
  auto& new_files = parsed.GetNewFiles();
  ASSERT_TRUE(new_files[0].second.marked_for_compaction);
  ASSERT_TRUE(!new_files[1].second.marked_for_compaction);
  ASSERT_TRUE(new_files[2].second.marked_for_compaction);
  ASSERT_EQ(3, new_files[0].second.fd.GetPathId());
  ASSERT_EQ(3, new_files[1].second.fd.GetPathId());
  ASSERT_EQ(0, new_files[2].second.fd.GetPathId());
}

TEST_F(VersionEditTest, ForwardCompatibleNewFile4) {
  static const uint64_t kBig = 1ull << 50;
  VersionEdit edit;
  edit.AddFile(3,
               FileDescriptor(300, 3, 100, 30),
               MakeFileBoundaryValues("foo", kBig + 500, kTypeValue),
               MakeFileBoundaryValues("zoo", kBig + 600, kTypeDeletion),
               true);
  edit.AddFile(4,
               FileDescriptor(301, 3, 100, 30),
               MakeFileBoundaryValues("foo", kBig + 501, kTypeValue),
               MakeFileBoundaryValues("zoo", kBig + 601, kTypeDeletion),
               false);
  edit.DeleteFile(4, 700);

  edit.SetComparatorName("foo");
  edit.SetLogNumber(kBig + 100);
  edit.SetNextFile(kBig + 200);
  edit.SetLastSequence(kBig + 1000);

  std::string encoded;

  edit.EncodeTo(&encoded);

  auto extractor = test::MakeBoundaryValuesExtractor();
  VersionEdit parsed;
  Status s = parsed.DecodeFrom(extractor.get(), encoded);
  ASSERT_TRUE(s.ok()) << s.ToString();
  auto& new_files = parsed.GetNewFiles();
  ASSERT_TRUE(new_files[0].second.marked_for_compaction);
  ASSERT_TRUE(!new_files[1].second.marked_for_compaction);
  ASSERT_EQ(3, new_files[0].second.fd.GetPathId());
  ASSERT_EQ(3, new_files[1].second.fd.GetPathId());
  ASSERT_EQ(1u, parsed.GetDeletedFiles().size());
}

TEST_F(VersionEditTest, NewFile4NotSupportedField) {
  static const uint64_t kBig = 1ull << 50;
  VersionEdit edit;
  edit.AddFile(3,
               FileDescriptor(300, 3, 100, 30),
               MakeFileBoundaryValues("foo", kBig + 500, kTypeValue),
               MakeFileBoundaryValues("zoo", kBig + 600, kTypeDeletion),
               true);

  edit.SetComparatorName("foo");
  edit.SetLogNumber(kBig + 100);
  edit.SetNextFile(kBig + 200);
  edit.SetLastSequence(kBig + 1000);

  std::string encoded;

  edit.EncodeTo(&encoded);

  auto extractor = test::MakeBoundaryValuesExtractor();
  VersionEdit parsed;
  Status s = parsed.DecodeFrom(extractor.get(), encoded);
  ASSERT_OK(s);
}

TEST_F(VersionEditTest, EncodeEmptyFile) {
  VersionEdit edit;
  edit.AddFile(0,
               FileDescriptor(0, 0, 0, 0),
               FileMetaData::BoundaryValues(),
               FileMetaData::BoundaryValues(),
               false);
  std::string buffer;
  ASSERT_TRUE(!edit.EncodeTo(&buffer));
}

TEST_F(VersionEditTest, ColumnFamilyTest) {
  VersionEdit edit;
  edit.SetColumnFamily(2);
  edit.AddColumnFamily("column_family");
  edit.SetMaxColumnFamily(5);
  TestEncodeDecode(edit);

  edit.Clear();
  edit.SetColumnFamily(3);
  edit.DropColumnFamily();
  TestEncodeDecode(edit);
}

}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
