// Copyright (c) Yugabyte, Inc.
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

#include "yb/util/logging.h"
#include <gtest/gtest.h>

#include "yb/gutil/stringprintf.h"

#include "yb/encryption/encrypted_file.h"
#include "yb/encryption/encryption_util.h"
#include "yb/encryption/header_manager.h"
#include "yb/encryption/header_manager_impl.h"
#include "yb/encryption/universe_key_manager.h"

#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/table/block_based_table_factory.h"
#include "yb/rocksdb/table/internal_iterator.h"
#include "yb/rocksdb/table/table_builder.h"
#include "yb/rocksdb/util/file_reader_writer.h"

#include "yb/rocksutil/rocksdb_encrypted_file_factory.h"

#include "yb/tserver/universe_key_test_util.h"

#include "yb/util/path_util.h"
#include "yb/util/status.h"
#include "yb/util/test_util.h"

using std::string;

using namespace std::literals;

using yb::tserver::GenerateTestUniverseKeyManager;

DECLARE_int64(encryption_counter_min);
DECLARE_int64(encryption_counter_max);
DECLARE_bool(TEST_encryption_use_openssl_compatible_counter_overflow);

namespace yb {

namespace {

std::string GetKey(int i) {
  return StringPrintf("key%09dSSSSSSSS", i);
}

std::string GetValue(int i) {
  return Format("value%d", i);
}

}  // anonymous namespace

class EncryptedSSTableTest : public YBTest, public testing::WithParamInterface<bool> {
 protected:
  void CounterOverflow(
      int num_keys, int64_t initial_counter);
};

INSTANTIATE_TEST_CASE_P(
    UseOpensslCompatibleCounterOverflow, EncryptedSSTableTest, ::testing::Bool());

void EncryptedSSTableTest::CounterOverflow(
    int num_keys, int64_t initial_counter) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_encryption_counter_min) = initial_counter;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_encryption_counter_max) = initial_counter;
  ANNOTATE_UNPROTECTED_WRITE(
      FLAGS_TEST_encryption_use_openssl_compatible_counter_overflow) = GetParam();

  string test_dir;
  ASSERT_OK(Env::Default()->GetTestDirectory(&test_dir));

  rocksdb::Options opts;
  const rocksdb::ImmutableCFOptions imoptions(opts);
  auto ikc = std::make_shared<rocksdb::InternalKeyComparator>(opts.comparator);
  std::vector<std::unique_ptr<rocksdb::IntTblPropCollectorFactory> >
    block_based_table_factories;
  rocksdb::CompressionOptions compression_opts;
  rocksdb::TableBuilderOptions table_builder_options(
      imoptions,
      ikc,
      block_based_table_factories,
      rocksdb::CompressionType::kSnappyCompression,
      compression_opts,
      /* skip_filters */ false);

  rocksdb::TableReaderOptions table_reader_options(
      imoptions,
      rocksdb::EnvOptions(),
      ikc,
      /*skip_filters=*/ false);

  auto universe_key_manager = GenerateTestUniverseKeyManager();

  auto header_manager = DefaultHeaderManager(universe_key_manager.get());
  auto env = yb::NewRocksDBEncryptedEnv(std::move(header_manager));

  auto file_name = JoinPathSegments(test_dir, "test-file");

  std::unique_ptr<rocksdb::WritableFile> base_file;
  ASSERT_OK(env->NewWritableFile(file_name, &base_file, rocksdb::EnvOptions()));
  rocksdb::WritableFileWriter base_writer(std::move(base_file), rocksdb::EnvOptions(),
      /* suspender */ nullptr);

  string data_file_name = file_name + ".sblock.0";
  std::unique_ptr<rocksdb::WritableFile> data_file;
  ASSERT_OK(env->NewWritableFile(data_file_name, &data_file, rocksdb::EnvOptions()));
  rocksdb::WritableFileWriter data_writer(std::move(data_file), rocksdb::EnvOptions(),
      /* suspender */ nullptr);

  rocksdb::BlockBasedTableFactory blk_based_tbl_factory;
  auto table_builder = blk_based_tbl_factory.NewTableBuilder(
      table_builder_options, 0, &base_writer, &data_writer);

  for (int i = 0; i < num_keys; ++i) {
    string key = GetKey(i);
    table_builder->Add(key, GetValue(i));
  }
  ASSERT_OK(table_builder->Finish());
  LOG(INFO) << "Wrote a file of total size " << table_builder->TotalFileSize()
      << ", base file size: " << table_builder->BaseFileSize();
  ASSERT_OK(base_writer.Flush());
  ASSERT_OK(data_writer.Flush());
  ASSERT_OK(base_writer.Close());
  ASSERT_OK(data_writer.Close());

  std::unique_ptr<rocksdb::RandomAccessFile> random_access_file;
  ASSERT_OK(env->NewRandomAccessFile(file_name, &random_access_file, rocksdb::EnvOptions()));
  auto base_file_size = ASSERT_RESULT(random_access_file->Size());
  ASSERT_EQ(base_file_size, table_builder->BaseFileSize());

  std::unique_ptr<rocksdb::RandomAccessFile> random_access_data_file;
  ASSERT_OK(env->NewRandomAccessFile(
      data_file_name, &random_access_data_file, rocksdb::EnvOptions()));
  auto data_file_reader = std::make_unique<rocksdb::RandomAccessFileReader>(
      std::move(random_access_data_file), env.get());

  auto* eraf = down_cast<encryption::EncryptedRandomAccessFile*>(random_access_file.get());
  auto* eraf_data = down_cast<encryption::EncryptedRandomAccessFile*>(random_access_file.get());

  ASSERT_TRUE(eraf != nullptr);
  ASSERT_TRUE(eraf_data != nullptr);
  ASSERT_EQ(0, eraf->TEST_GetNumOverflowWorkarounds());
  ASSERT_EQ(0, eraf_data->TEST_GetNumOverflowWorkarounds());

  size_t raw_size = ASSERT_RESULT(Env::Default()->GetFileSize(file_name));
  LOG(INFO) << "Raw file size: " << raw_size;

  auto random_access_file_reader = std::make_unique<rocksdb::RandomAccessFileReader>(
      std::move(random_access_file));

  std::unique_ptr<rocksdb::TableReader> table_reader;

  ASSERT_OK(blk_based_tbl_factory.NewTableReader(
      table_reader_options, std::move(random_access_file_reader),
      base_file_size,
      &table_reader,
      rocksdb::DataIndexLoadMode::PRELOAD_ON_OPEN,
      rocksdb::PrefetchFilter::YES));

  table_reader->SetDataFileReader(std::move(data_file_reader));

  auto it = std::unique_ptr<rocksdb::InternalIterator>(
      table_reader->NewIterator(rocksdb::ReadOptions()));
  it->SeekToFirst();
  int i = 0;
  while (ASSERT_RESULT(it->CheckedValid())) {
    ASSERT_EQ(it->key(), GetKey(i));
    ASSERT_EQ(it->value(), GetValue(i));
    i++;
    it->Next();
  }
  ASSERT_EQ(num_keys, i);
  ASSERT_GE(eraf->TEST_GetNumOverflowWorkarounds(), 0);
  ASSERT_GE(eraf_data->TEST_GetNumOverflowWorkarounds(), 0);
}

TEST_P(EncryptedSSTableTest, CounterOverflow10MKeys) {
  // Note that only three zeros are there in the end of the initial counter below. We are trying to
  // get a counter that is 65536 bytes (4096 encryption blocks) away from overflow.
  CounterOverflow(10 * 1000 * 1000, 0xfffff000);
}

TEST_P(EncryptedSSTableTest, CounterOverflow100000Keys) {
  // This test fails if meta block checksums are not being verified.
  // https://github.com/yugabyte/yugabyte-db/issues/3974
  CounterOverflow(100 * 1000, 0xffffff00);
}

} // namespace yb
