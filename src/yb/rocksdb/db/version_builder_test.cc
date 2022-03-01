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

#include <string>

#include <gtest/gtest.h>

#include "yb/rocksdb/db/version_edit.h"
#include "yb/rocksdb/db/version_set.h"
#include "yb/rocksdb/env.h"

#include "yb/util/string_util.h"
#include "yb/rocksdb/util/testutil.h"

namespace rocksdb {

class VersionBuilderTest : public RocksDBTest {
 public:
  const Comparator* ucmp_;
  InternalKeyComparatorPtr icmp_;
  Options options_;
  ImmutableCFOptions ioptions_;
  MutableCFOptions mutable_cf_options_;
  VersionStorageInfo vstorage_;
  uint32_t file_num_;
  CompactionOptionsFIFO fifo_options_;
  std::vector<uint64_t> size_being_compacted_;

  VersionBuilderTest()
      : ucmp_(BytewiseComparator()),
        icmp_(std::make_shared<InternalKeyComparator>(ucmp_)),
        ioptions_(options_),
        mutable_cf_options_(options_, ioptions_),
        vstorage_(icmp_, ucmp_, options_.num_levels, kCompactionStyleLevel,
                 nullptr),
        file_num_(1) {
    mutable_cf_options_.RefreshDerivedOptions(ioptions_);
    size_being_compacted_.resize(options_.num_levels);
  }

  ~VersionBuilderTest() {
    for (int i = 0; i < vstorage_.num_levels(); i++) {
      for (auto* f : vstorage_.LevelFiles(i)) {
        if (--f->refs == 0) {
          delete f;
        }
      }
    }
  }

  FileBoundaryValues<InternalKey> GetBoundaryValues(const char* ukey,
                                                    SequenceNumber key_seqno = 100,
                                                    SequenceNumber boundary_seqno = 200) {
    auto result = MakeFileBoundaryValues(ukey, key_seqno, kTypeValue);
    result.seqno = boundary_seqno;
    return result;
  }

  void Add(int level, uint32_t file_number, const char* smallest,
           const char* largest, uint64_t file_size = 0, uint32_t path_id = 0,
           SequenceNumber smallest_seq = 100, SequenceNumber largest_seq = 100,
           uint64_t num_entries = 0, uint64_t num_deletions = 0,
           bool sampled = false, SequenceNumber smallest_seqno = 0,
           SequenceNumber largest_seqno = 0) {
    assert(level < vstorage_.num_levels());
    FileMetaData* f = new FileMetaData;
    f->fd = FileDescriptor(file_number, path_id, file_size, 64);
    f->smallest = GetBoundaryValues(smallest, smallest_seq, smallest_seq);
    f->largest = GetBoundaryValues(largest, largest_seq, largest_seq);
    f->compensated_file_size = file_size;
    f->refs = 0;
    f->num_entries = num_entries;
    f->num_deletions = num_deletions;
    vstorage_.AddFile(level, f);
    if (sampled) {
      f->init_stats_from_file = true;
      vstorage_.UpdateAccumulatedStats(f);
    }
  }

  void UpdateVersionStorageInfo() {
    vstorage_.UpdateFilesByCompactionPri(mutable_cf_options_);
    vstorage_.UpdateNumNonEmptyLevels();
    vstorage_.GenerateFileIndexer();
    vstorage_.GenerateLevelFilesBrief();
    vstorage_.CalculateBaseBytes(ioptions_, mutable_cf_options_);
    vstorage_.GenerateLevel0NonOverlapping();
    vstorage_.SetFinalized();
  }
};

void UnrefFilesInVersion(VersionStorageInfo* new_vstorage) {
  for (int i = 0; i < new_vstorage->num_levels(); i++) {
    for (auto* f : new_vstorage->LevelFiles(i)) {
      if (--f->refs == 0) {
        delete f;
      }
    }
  }
}

TEST_F(VersionBuilderTest, ApplyAndSaveTo) {
  Add(0, 1U, "150", "200", 100U);

  Add(1, 66U, "150", "200", 100U);
  Add(1, 88U, "201", "300", 100U);

  Add(2, 6U, "150", "179", 100U);
  Add(2, 7U, "180", "220", 100U);
  Add(2, 8U, "221", "300", 100U);

  Add(3, 26U, "150", "170", 100U);
  Add(3, 27U, "171", "179", 100U);
  Add(3, 28U, "191", "220", 100U);
  Add(3, 29U, "221", "300", 100U);
  UpdateVersionStorageInfo();

  VersionEdit version_edit;
  version_edit.AddTestFile(2,
                           FileDescriptor(666, 0, 100U, 30U),
                           GetBoundaryValues("301"),
                           GetBoundaryValues("350"),
                           false);
  version_edit.DeleteFile(3, 27U);

  EnvOptions env_options;

  VersionBuilder version_builder(env_options, nullptr, &vstorage_);

  VersionStorageInfo new_vstorage(icmp_, ucmp_, options_.num_levels,
                                  kCompactionStyleLevel, nullptr);
  version_builder.Apply(&version_edit);
  version_builder.SaveTo(&new_vstorage);

  ASSERT_EQ(400U, new_vstorage.NumLevelBytes(2));
  ASSERT_EQ(300U, new_vstorage.NumLevelBytes(3));

  UnrefFilesInVersion(&new_vstorage);
}

TEST_F(VersionBuilderTest, ApplyAndSaveToDynamic) {
  ioptions_.level_compaction_dynamic_level_bytes = true;

  Add(0, 1U, "150", "200", 100U, 0, 200U, 200U, 0, 0, false, 200U, 200U);
  Add(0, 88U, "201", "300", 100U, 0, 100U, 100U, 0, 0, false, 100U, 100U);

  Add(4, 6U, "150", "179", 100U);
  Add(4, 7U, "180", "220", 100U);
  Add(4, 8U, "221", "300", 100U);

  Add(5, 26U, "150", "170", 100U);
  Add(5, 27U, "171", "179", 100U);
  UpdateVersionStorageInfo();

  VersionEdit version_edit;
  version_edit.AddTestFile(3,
                           FileDescriptor(666, 0, 100U, 30U),
                           GetBoundaryValues("301"),
                           GetBoundaryValues("350"),
                           false);
  version_edit.DeleteFile(0, 1U);
  version_edit.DeleteFile(0, 88U);

  EnvOptions env_options;

  VersionBuilder version_builder(env_options, nullptr, &vstorage_);

  VersionStorageInfo new_vstorage(icmp_, ucmp_, options_.num_levels,
                                  kCompactionStyleLevel, nullptr);
  version_builder.Apply(&version_edit);
  version_builder.SaveTo(&new_vstorage);

  ASSERT_EQ(0U, new_vstorage.NumLevelBytes(0));
  ASSERT_EQ(100U, new_vstorage.NumLevelBytes(3));
  ASSERT_EQ(300U, new_vstorage.NumLevelBytes(4));
  ASSERT_EQ(200U, new_vstorage.NumLevelBytes(5));

  UnrefFilesInVersion(&new_vstorage);
}

TEST_F(VersionBuilderTest, ApplyAndSaveToDynamic2) {
  ioptions_.level_compaction_dynamic_level_bytes = true;

  Add(0, 1U, "150", "200", 100U, 0, 200U, 200U, 0, 0, false, 200U, 200U);
  Add(0, 88U, "201", "300", 100U, 0, 100U, 100U, 0, 0, false, 100U, 100U);

  Add(4, 6U, "150", "179", 100U);
  Add(4, 7U, "180", "220", 100U);
  Add(4, 8U, "221", "300", 100U);

  Add(5, 26U, "150", "170", 100U);
  Add(5, 27U, "171", "179", 100U);
  UpdateVersionStorageInfo();

  VersionEdit version_edit;
  version_edit.AddTestFile(4,
                           FileDescriptor(666, 0, 100U, 30U),
                           GetBoundaryValues("301"),
                           GetBoundaryValues("350"),
                           false);
  version_edit.DeleteFile(0, 1U);
  version_edit.DeleteFile(0, 88U);
  version_edit.DeleteFile(4, 6U);
  version_edit.DeleteFile(4, 7U);
  version_edit.DeleteFile(4, 8U);

  EnvOptions env_options;

  VersionBuilder version_builder(env_options, nullptr, &vstorage_);

  VersionStorageInfo new_vstorage(icmp_, ucmp_, options_.num_levels,
                                  kCompactionStyleLevel, nullptr);
  version_builder.Apply(&version_edit);
  version_builder.SaveTo(&new_vstorage);

  ASSERT_EQ(0U, new_vstorage.NumLevelBytes(0));
  ASSERT_EQ(100U, new_vstorage.NumLevelBytes(4));
  ASSERT_EQ(200U, new_vstorage.NumLevelBytes(5));

  UnrefFilesInVersion(&new_vstorage);
}

TEST_F(VersionBuilderTest, ApplyMultipleAndSaveTo) {
  UpdateVersionStorageInfo();

  VersionEdit version_edit;
  version_edit.AddTestFile(2,
                           FileDescriptor(666, 0, 100U, 30U),
                           GetBoundaryValues("301"),
                           GetBoundaryValues("350"),
                           false);
  version_edit.AddTestFile(2,
                           FileDescriptor(676, 0, 100U, 30U),
                           GetBoundaryValues("401"),
                           GetBoundaryValues("450"),
                           false);
  version_edit.AddTestFile(2,
                           FileDescriptor(636, 0, 100U, 30U),
                           GetBoundaryValues("601"),
                           GetBoundaryValues("650"),
                           false);
  version_edit.AddTestFile(2,
                           FileDescriptor(616, 0, 100U, 30U),
                           GetBoundaryValues("501"),
                           GetBoundaryValues("550"),
                           false);
  version_edit.AddTestFile(2,
                           FileDescriptor(606, 0, 100U, 30U),
                           GetBoundaryValues("701"),
                           GetBoundaryValues("750"),
                           false);

  EnvOptions env_options;

  VersionBuilder version_builder(env_options, nullptr, &vstorage_);

  VersionStorageInfo new_vstorage(icmp_, ucmp_, options_.num_levels,
                                  kCompactionStyleLevel, nullptr);
  version_builder.Apply(&version_edit);
  version_builder.SaveTo(&new_vstorage);

  ASSERT_EQ(500U, new_vstorage.NumLevelBytes(2));

  UnrefFilesInVersion(&new_vstorage);
}

TEST_F(VersionBuilderTest, ApplyDeleteAndSaveTo) {
  UpdateVersionStorageInfo();

  EnvOptions env_options;
  VersionBuilder version_builder(env_options, nullptr, &vstorage_);
  VersionStorageInfo new_vstorage(icmp_, ucmp_, options_.num_levels,
                                  kCompactionStyleLevel, nullptr);

  VersionEdit version_edit;
  version_edit.AddTestFile(2,
                           FileDescriptor(666, 0, 100U, 30U),
                           GetBoundaryValues("301"),
                           GetBoundaryValues("350"),
                           false);
  version_edit.AddTestFile(2,
                           FileDescriptor(676, 0, 100U, 30U),
                           GetBoundaryValues("401"),
                           GetBoundaryValues("450"),
                           false);
  version_edit.AddTestFile(2,
                           FileDescriptor(636, 0, 100U, 30U),
                           GetBoundaryValues("601"),
                           GetBoundaryValues("650"),
                           false);
  version_edit.AddTestFile(2,
                           FileDescriptor(616, 0, 100U, 30U),
                           GetBoundaryValues("501"),
                           GetBoundaryValues("550"),
                           false);
  version_edit.AddTestFile(2,
                           FileDescriptor(606, 0, 100U, 30U),
                           GetBoundaryValues("701"),
                           GetBoundaryValues("750"),
                           false);
  version_builder.Apply(&version_edit);

  VersionEdit version_edit2;
  version_edit.AddTestFile(2,
                           FileDescriptor(808, 0, 100U, 30U),
                           GetBoundaryValues("901"),
                           GetBoundaryValues("950"),
                           false);
  version_edit2.DeleteFile(2, 616);
  version_edit2.DeleteFile(2, 636);
  version_edit.AddTestFile(2,
                           FileDescriptor(806, 0, 100U, 30U),
                           GetBoundaryValues("801"),
                           GetBoundaryValues("850"),
                           false);
  version_builder.Apply(&version_edit2);

  version_builder.SaveTo(&new_vstorage);

  ASSERT_EQ(300U, new_vstorage.NumLevelBytes(2));

  UnrefFilesInVersion(&new_vstorage);
}

TEST_F(VersionBuilderTest, EstimatedActiveKeys) {
  const uint32_t kTotalSamples = 20;
  const uint32_t kNumLevels = 5;
  const uint32_t kFilesPerLevel = 8;
  const uint32_t kNumFiles = kNumLevels * kFilesPerLevel;
  const uint32_t kEntriesPerFile = 1000;
  const uint32_t kDeletionsPerFile = 100;
  for (uint32_t i = 0; i < kNumFiles; ++i) {
    Add(static_cast<int>(i / kFilesPerLevel), i + 1,
        ToString((i + 100) * 1000).c_str(),
        ToString((i + 100) * 1000 + 999).c_str(),
        100U,  0, 100, 100,
        kEntriesPerFile, kDeletionsPerFile,
        (i < kTotalSamples));
  }
  // minus 2X for the number of deletion entries because:
  // 1x for deletion entry does not count as a data entry.
  // 1x for each deletion entry will actually remove one data entry.
  ASSERT_EQ(vstorage_.GetEstimatedActiveKeys(),
            (kEntriesPerFile - 2 * kDeletionsPerFile) * kNumFiles);
}

}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
