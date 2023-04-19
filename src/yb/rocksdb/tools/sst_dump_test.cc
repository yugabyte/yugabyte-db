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
// Copyright (c) 2012 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.


#include <stdint.h>

#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/db/filename.h"
#include "yb/rocksdb/filter_policy.h"
#include "yb/rocksdb/sst_dump_tool.h"
#include "yb/rocksdb/table/block_based_table_factory.h"
#include "yb/rocksdb/table/table_builder.h"
#include "yb/rocksdb/util/file_reader_writer.h"
#include "yb/rocksdb/util/testutil.h"

#include "yb/util/test_util.h"

using std::unique_ptr;

namespace rocksdb {

const uint32_t optLength = 100;

namespace {
static std::string MakeKey(int i) {
  char buf[100];
  snprintf(buf, sizeof(buf), "k_%04d", i);
  InternalKey key(std::string(buf), 0, ValueType::kTypeValue);
  return key.Encode().ToString();
}

static std::string MakeValue(int i) {
  char buf[100];
  snprintf(buf, sizeof(buf), "v_%04d", i);
  InternalKey key(std::string(buf), 0, ValueType::kTypeValue);
  return key.Encode().ToString();
}

void createSST(const std::string& base_file_name,
               const BlockBasedTableOptions& table_options) {
  std::shared_ptr<rocksdb::TableFactory> tf;
  tf.reset(new rocksdb::BlockBasedTableFactory(table_options));

  unique_ptr<WritableFile> base_file;
  Env* env = Env::Default();
  EnvOptions env_options;
  ReadOptions read_options;
  Options opts;
  const ImmutableCFOptions imoptions(opts);
  auto ikc = std::make_shared<rocksdb::InternalKeyComparator>(opts.comparator);
  unique_ptr<TableBuilder> tb;

  ASSERT_OK(env->NewWritableFile(base_file_name, &base_file, env_options));
  opts.table_factory = tf;
  std::vector<std::unique_ptr<IntTblPropCollectorFactory> >
      int_tbl_prop_collector_factories;
  unique_ptr<WritableFileWriter> base_file_writer(
      new WritableFileWriter(std::move(base_file), EnvOptions()));
  unique_ptr<WritableFileWriter> data_file_writer;
  if (opts.table_factory->IsSplitSstForWriteSupported()) {
    unique_ptr<WritableFile> data_file;
    ASSERT_OK(env->NewWritableFile(
        TableBaseToDataFileName(base_file_name), &data_file, env_options));
    data_file_writer.reset(new WritableFileWriter(std::move(data_file), EnvOptions()));
  }
  tb = opts.table_factory->NewTableBuilder(
      TableBuilderOptions(imoptions,
                          ikc,
                          int_tbl_prop_collector_factories,
                          CompressionType::kNoCompression,
                          CompressionOptions(),
                          /* skip_filters */ false),
      TablePropertiesCollectorFactory::Context::kUnknownColumnFamily,
      base_file_writer.get(), data_file_writer.get());

  // Populate slightly more than 1K keys
  uint32_t num_keys = 1024;
  for (uint32_t i = 0; i < num_keys; i++) {
    tb->Add(MakeKey(i), MakeValue(i));
  }
  ASSERT_OK(tb->Finish());
  ASSERT_OK(base_file_writer->Close());
}

void cleanup(const std::string& file_name) {
  Env* env = Env::Default();
  ASSERT_OK(env->DeleteFile(file_name));
  std::string outfile_name = file_name.substr(0, file_name.length() - 4);
  outfile_name.append("_dump.txt");
  env->CleanupFile(outfile_name);
}

}  // namespace

// Test for sst dump tool "raw" mode
class SSTDumpToolTest : public RocksDBTest {
 public:
  BlockBasedTableOptions table_options_;

  SSTDumpToolTest() {}

  ~SSTDumpToolTest() {}
};

TEST_F(SSTDumpToolTest, EmptyFilter) {
  std::string file_name = "rocksdb_sst_test.sst";
  createSST(file_name, table_options_);

  char* usage[3];
  for (int i = 0; i < 3; i++) {
    usage[i] = new char[optLength];
  }
  snprintf(usage[0], optLength, "./sst_dump");
  snprintf(usage[1], optLength, "--command=raw");
  snprintf(usage[2], optLength, "--file=rocksdb_sst_test.sst");

  rocksdb::SSTDumpTool tool(nullptr);
  ASSERT_TRUE(!tool.Run(3, usage));

  cleanup(file_name);
  for (int i = 0; i < 3; i++) {
    delete[] usage[i];
  }
}

TEST_F(SSTDumpToolTest, FilterBlock) {
  table_options_.filter_policy.reset(rocksdb::NewBloomFilterPolicy(10, true));
  std::string file_name = "rocksdb_sst_test.sst";
  createSST(file_name, table_options_);

  char* usage[3];
  for (int i = 0; i < 3; i++) {
    usage[i] = new char[optLength];
  }
  snprintf(usage[0], optLength, "./sst_dump");
  snprintf(usage[1], optLength, "--command=raw");
  snprintf(usage[2], optLength, "--file=rocksdb_sst_test.sst");

  rocksdb::SSTDumpTool tool;
  ASSERT_TRUE(!tool.Run(3, usage));

  cleanup(file_name);
  for (int i = 0; i < 3; i++) {
    delete[] usage[i];
  }
}

TEST_F(SSTDumpToolTest, FullFilterBlock) {
  table_options_.filter_policy.reset(rocksdb::NewBloomFilterPolicy(10, false));
  std::string file_name = "rocksdb_sst_test.sst";
  createSST(file_name, table_options_);

  char* usage[3];
  for (int i = 0; i < 3; i++) {
    usage[i] = new char[optLength];
  }
  snprintf(usage[0], optLength, "./sst_dump");
  snprintf(usage[1], optLength, "--command=raw");
  snprintf(usage[2], optLength, "--file=rocksdb_sst_test.sst");

  rocksdb::SSTDumpTool tool;
  ASSERT_TRUE(!tool.Run(3, usage));

  cleanup(file_name);
  for (int i = 0; i < 3; i++) {
    delete[] usage[i];
  }
}

TEST_F(SSTDumpToolTest, GetProperties) {
  table_options_.filter_policy.reset(rocksdb::NewBloomFilterPolicy(10, false));
  std::string file_name = "rocksdb_sst_test.sst";
  createSST(file_name, table_options_);

  char* usage[3];
  for (int i = 0; i < 3; i++) {
    usage[i] = new char[optLength];
  }
  snprintf(usage[0], optLength, "./sst_dump");
  snprintf(usage[1], optLength, "--show_properties");
  snprintf(usage[2], optLength, "--file=rocksdb_sst_test.sst");

  rocksdb::SSTDumpTool tool;
  ASSERT_TRUE(!tool.Run(3, usage));

  cleanup(file_name);
  for (int i = 0; i < 3; i++) {
    delete[] usage[i];
  }
}

TEST_F(SSTDumpToolTest, CompressedSizes) {
  table_options_.filter_policy.reset(rocksdb::NewBloomFilterPolicy(10, false));
  std::string file_name = "rocksdb_sst_test.sst";
  createSST(file_name, table_options_);

  char* usage[3];
  for (int i = 0; i < 3; i++) {
    usage[i] = new char[optLength];
  }

  snprintf(usage[0], optLength, "./sst_dump");
  snprintf(usage[1], optLength, "--show_compression_sizes");
  snprintf(usage[2], optLength, "--file=rocksdb_sst_test.sst");
  rocksdb::SSTDumpTool tool;
  ASSERT_TRUE(!tool.Run(3, usage));

  cleanup(file_name);
  for (int i = 0; i < 3; i++) {
    delete[] usage[i];
  }
}
}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
