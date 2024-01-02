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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "yb/rocksdb/db/version_edit.h"

#include <string>
#include <gtest/gtest.h>
#include "yb/rocksdb/env.h"
#include "yb/util/test_macros.h"
#include "yb/rocksdb/util/testutil.h"

namespace rocksdb {

namespace {

void TestEncodeDecode(const VersionEdit &edit) {
  auto extractor = test::MakeBoundaryValuesExtractor();
  std::string encoded, encoded2;
  edit.AppendEncodedTo(&encoded);
  VersionEdit parsed;
  Status s = parsed.DecodeFrom(extractor.get(), encoded);
  ASSERT_TRUE(s.ok()) << s.ToString();

  auto str = edit.DebugString();
  auto str2 = parsed.DebugString();
  ASSERT_EQ(str, str2);

  parsed.AppendEncodedTo(&encoded2);
  ASSERT_EQ(encoded, encoded2);
}

constexpr uint64_t kBig = 1ull << 50;

} // namespace

class VersionEditTest : public RocksDBTest {};

void SetupVersionEdit(VersionEdit* edit) {
  edit->SetComparatorName("foo");
  edit->SetLogNumber(kBig + 100);
  edit->SetNextFile(kBig + 200);
  edit->SetLastSequence(kBig + 1000);
  test::TestUserFrontier frontier(kBig + 100);
  edit->UpdateFlushedFrontier(frontier.Clone());
}

TEST_F(VersionEditTest, EncodeDecode) {
  static const uint32_t kBig32Bit = 1ull << 30;

  VersionEdit edit;
  for (int i = 0; i < 4; i++) {
    TestEncodeDecode(edit);
    auto smallest = MakeFileBoundaryValues("foo", kBig + 500 + i, kTypeValue);
    auto largest = MakeFileBoundaryValues("zoo", kBig + 600 + i, kTypeDeletion);
    smallest.user_values.push_back(test::MakeLeftBoundaryValue("left"));
    largest.user_values.push_back(test::MakeRightBoundaryValue("right"));
    edit.AddTestFile(3,
                     FileDescriptor(kBig + 300 + i, kBig32Bit + 400 + i, 0, 0),
                     smallest,
                     largest,
                     false);
    edit.DeleteFile(4, kBig + 700 + i);
  }

  SetupVersionEdit(&edit);
  TestEncodeDecode(edit);
}

TEST_F(VersionEditTest, EncodeDecodeNewFile4) {
  VersionEdit edit;
  edit.AddTestFile(3,
                   FileDescriptor(300, 3, 100, 30),
                   MakeFileBoundaryValues("foo", kBig + 500, kTypeValue),
                   MakeFileBoundaryValues("zoo", kBig + 600, kTypeDeletion),
                   true);
  edit.AddTestFile(4,
                   FileDescriptor(301, 3, 100, 30),
                   MakeFileBoundaryValues("foo", kBig + 501, kTypeValue),
                   MakeFileBoundaryValues("zoo", kBig + 601, kTypeDeletion),
                   false);
  edit.AddTestFile(5,
                   FileDescriptor(302, 0, 100, 30),
                   MakeFileBoundaryValues("foo", kBig + 502, kTypeValue),
                   MakeFileBoundaryValues("zoo", kBig + 602, kTypeDeletion),
                   true);

  edit.DeleteFile(4, 700);

  SetupVersionEdit(&edit);
  TestEncodeDecode(edit);

  auto extractor = test::MakeBoundaryValuesExtractor();
  std::string encoded, encoded2;
  edit.AppendEncodedTo(&encoded);
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
  VersionEdit edit;
  edit.AddTestFile(3,
                   FileDescriptor(300, 3, 100, 30),
                   MakeFileBoundaryValues("foo", kBig + 500, kTypeValue),
                   MakeFileBoundaryValues("zoo", kBig + 600, kTypeDeletion),
                   true);
  edit.AddTestFile(4,
                   FileDescriptor(301, 3, 100, 30),
                   MakeFileBoundaryValues("foo", kBig + 501, kTypeValue),
                   MakeFileBoundaryValues("zoo", kBig + 601, kTypeDeletion),
                   false);
  edit.DeleteFile(4, 700);

  SetupVersionEdit(&edit);

  std::string encoded;

  edit.AppendEncodedTo(&encoded);

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
  VersionEdit edit;
  edit.AddTestFile(3,
                   FileDescriptor(300, 3, 100, 30),
                   MakeFileBoundaryValues("foo", kBig + 500, kTypeValue),
                   MakeFileBoundaryValues("zoo", kBig + 600, kTypeDeletion),
                   true);

  SetupVersionEdit(&edit);

  std::string encoded;

  edit.AppendEncodedTo(&encoded);

  auto extractor = test::MakeBoundaryValuesExtractor();
  VersionEdit parsed;
  Status s = parsed.DecodeFrom(extractor.get(), encoded);
  ASSERT_OK(s);
}

TEST_F(VersionEditTest, EncodeEmptyFile) {
  VersionEdit edit;
  edit.AddTestFile(0,
                   FileDescriptor(0, 0, 0, 0),
                   FileMetaData::BoundaryValues(),
                   FileMetaData::BoundaryValues(),
                   false);
  std::string buffer;
  ASSERT_TRUE(!edit.AppendEncodedTo(&buffer));
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
