// Copyright (c) YugaByte, Inc.
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

#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/rocksdb/table.h"

#include "yb/util/test_util.h"

DECLARE_int32(num_cpus);
DECLARE_int32(rocksdb_max_background_flushes);
DECLARE_bool(rocksdb_disable_compactions);
DECLARE_int32(rocksdb_base_background_compactions);
DECLARE_int32(rocksdb_max_background_compactions);
DECLARE_int32(priority_thread_pool_size);
DECLARE_int32(block_restart_interval);
DECLARE_int32(index_block_restart_interval);

namespace yb {
namespace docdb {

class DocDBRocksDBUtilTest : public YBTest {};

TEST_F(DocDBRocksDBUtilTest, MaxBackgroundFlushesDefault) {
  FLAGS_num_cpus = 16;
  auto options = TEST_AutoInitFromRocksDBFlags();
  CHECK_EQ(options.max_background_flushes, 3);
}

TEST_F(DocDBRocksDBUtilTest, MaxBackgroundFlushesDefaultLimit) {
  FLAGS_num_cpus = 32;
  auto options = TEST_AutoInitFromRocksDBFlags();
  CHECK_EQ(options.max_background_flushes, 4);
}

TEST_F(DocDBRocksDBUtilTest, MaxBackgroundFlushesCompactionsDisabled) {
  FLAGS_rocksdb_max_background_flushes = 10;
  FLAGS_rocksdb_disable_compactions = true;
  auto options = TEST_AutoInitFromRocksDBFlags();
  CHECK_EQ(options.max_background_flushes, 10);
}

TEST_F(DocDBRocksDBUtilTest, BaseBackgroundCompactionsDefault) {
  FLAGS_rocksdb_max_background_compactions = 10;
  auto options = TEST_AutoInitFromRocksDBFlags();
  CHECK_EQ(options.base_background_compactions, 10);
}

TEST_F(DocDBRocksDBUtilTest, BaseBackgroundCompactionsDisabled) {
  FLAGS_rocksdb_disable_compactions = true;
  auto options = TEST_AutoInitFromRocksDBFlags();
  CHECK_EQ(options.base_background_compactions, -1);
}

TEST_F(DocDBRocksDBUtilTest, BaseBackgroundCompactionsExplicit) {
  FLAGS_rocksdb_base_background_compactions = 23;
  auto options = TEST_AutoInitFromRocksDBFlags();
  CHECK_EQ(options.base_background_compactions, 23);
}

TEST_F(DocDBRocksDBUtilTest, MaxBackgroundCompactionsDefault) {
  FLAGS_num_cpus = 12;
  auto options = TEST_AutoInitFromRocksDBFlags();
  CHECK_EQ(options.max_background_compactions, 3);
}

TEST_F(DocDBRocksDBUtilTest, MaxBackgroundCompactionsDisabled) {
  FLAGS_rocksdb_disable_compactions = true;
  auto options = TEST_AutoInitFromRocksDBFlags();
  CHECK_EQ(options.max_background_compactions, 1);
}

TEST_F(DocDBRocksDBUtilTest, MaxBackgroundCompactionsExplicit) {
  FLAGS_rocksdb_max_background_compactions = 23;
  auto options = TEST_AutoInitFromRocksDBFlags();
  CHECK_EQ(options.max_background_compactions, 23);
}

TEST_F(DocDBRocksDBUtilTest, PriorityThreadPoolSizeDefaultLowCpus) {
  FLAGS_num_cpus = 3;
  CHECK_EQ(GetGlobalRocksDBPriorityThreadPoolSize(), 1);
}

TEST_F(DocDBRocksDBUtilTest, PriorityThreadPoolSizeDefaultFewCpus) {
  FLAGS_num_cpus = 5;
  CHECK_EQ(GetGlobalRocksDBPriorityThreadPoolSize(), 2);
}

TEST_F(DocDBRocksDBUtilTest, PriorityThreadPoolSizeDefaultManyCpus) {
  FLAGS_num_cpus = 10;
  CHECK_EQ(GetGlobalRocksDBPriorityThreadPoolSize(), 4);
}

TEST_F(DocDBRocksDBUtilTest, PriorityThreadPoolSizeTakesExplicitSetting) {
  FLAGS_priority_thread_pool_size = 16;
  CHECK_EQ(GetGlobalRocksDBPriorityThreadPoolSize(), 16);
}

TEST_F(DocDBRocksDBUtilTest, PriorityThreadPoolSizeTakesMaxBackgroundCompaction) {
  FLAGS_rocksdb_max_background_compactions = 4;
  CHECK_EQ(GetGlobalRocksDBPriorityThreadPoolSize(), 4);
}

TEST_F(DocDBRocksDBUtilTest, PriorityThreadPoolSizeCompactionDisabled) {
  FLAGS_rocksdb_disable_compactions = true;
  CHECK_EQ(GetGlobalRocksDBPriorityThreadPoolSize(), 1);
}

TEST_F(DocDBRocksDBUtilTest, MinBlockRestartInterval) {
  FLAGS_block_restart_interval = 0;
  auto blockBasedOptions = TEST_AutoInitFromRocksDbTableFlags();
  CHECK_EQ(blockBasedOptions.block_restart_interval, 16);
}

TEST_F(DocDBRocksDBUtilTest, MaxBlockRestartInterval) {
  FLAGS_block_restart_interval = 512;
  auto blockBasedOptions = TEST_AutoInitFromRocksDbTableFlags();
  CHECK_EQ(blockBasedOptions.block_restart_interval, 256);
}

TEST_F(DocDBRocksDBUtilTest, ValidBlockRestartInterval) {
  FLAGS_block_restart_interval = 8;
  auto blockBasedOptions = TEST_AutoInitFromRocksDbTableFlags();
  CHECK_EQ(blockBasedOptions.block_restart_interval, 8);
}

TEST_F(DocDBRocksDBUtilTest, DefaultBlockRestartInterval) {
  auto blockBasedOptions = TEST_AutoInitFromRocksDbTableFlags();
  CHECK_EQ(blockBasedOptions.block_restart_interval, 16);
}

TEST_F(DocDBRocksDBUtilTest, MinIndexBlockRestartInterval) {
  FLAGS_index_block_restart_interval = 0;
  auto blockBasedOptions = TEST_AutoInitFromRocksDbTableFlags();
  CHECK_EQ(blockBasedOptions.index_block_restart_interval, 1);
}

TEST_F(DocDBRocksDBUtilTest, MaxIndexBlockRestartInterval) {
  FLAGS_index_block_restart_interval = 512;
  auto blockBasedOptions = TEST_AutoInitFromRocksDbTableFlags();
  CHECK_EQ(blockBasedOptions.index_block_restart_interval, 256);
}

TEST_F(DocDBRocksDBUtilTest, ValidIndexBlockRestartInterval) {
  FLAGS_index_block_restart_interval = 8;
  auto blockBasedOptions = TEST_AutoInitFromRocksDbTableFlags();
  CHECK_EQ(blockBasedOptions.index_block_restart_interval, 8);
}

TEST_F(DocDBRocksDBUtilTest, DefaultIndexBlockRestartInterval) {
  auto blockBasedOptions = TEST_AutoInitFromRocksDbTableFlags();
  CHECK_EQ(blockBasedOptions.index_block_restart_interval, 1);
}

}  // namespace docdb
}  // namespace yb
