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

#include "yb/rocksdb/tools/sst_dump_tool_imp.h"

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <inttypes.h>
#include <map>
#include <sstream>
#include <vector>

#include "yb/rocksdb/db/filename.h"
#include "yb/rocksdb/db/memtable.h"
#include "yb/rocksdb/db/write_batch_internal.h"
#include "yb/rocksdb/db.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/immutable_options.h"
#include "yb/rocksdb/iterator.h"
#include "yb/rocksdb/slice_transform.h"
#include "yb/rocksdb/status.h"
#include "yb/rocksdb/table_properties.h"
#include "yb/rocksdb/table/block.h"
#include "yb/rocksdb/table/block_based_table_builder.h"
#include "yb/rocksdb/table/block_based_table_factory.h"
#include "yb/rocksdb/table/block_builder.h"
#include "yb/rocksdb/table/format.h"
#include "yb/rocksdb/table/meta_blocks.h"
#include "yb/rocksdb/table/plain_table_factory.h"
#include "yb/rocksdb/tools/ldb_cmd.h"
#include "yb/rocksdb/util/random.h"

#include "yb/rocksdb/port/port.h"

#include "yb/docdb/docdb_debug.h"

#include "yb/util/status_log.h"

using yb::docdb::StorageDbType;

namespace rocksdb {

using std::dynamic_pointer_cast;
using std::unique_ptr;
using std::shared_ptr;

std::string DocDBKVFormatter::Format(
    const yb::Slice&, const yb::Slice&, yb::docdb::StorageDbType) const {
  CHECK(false) << "unimplemented";
  return "";
}

SstFileReader::SstFileReader(
    const std::string& file_path, bool verify_checksum, OutputFormat output_format,
    const DocDBKVFormatter* formatter)
    : file_name_(file_path),
      read_num_(0),
      verify_checksum_(verify_checksum),
      output_format_(output_format),
      docdb_kv_formatter_(formatter),
      ioptions_(options_),
      internal_comparator_(std::make_shared<InternalKeyComparator>(BytewiseComparator())) {
  fprintf(stdout, "Process %s\n", file_path.c_str());
  init_result_ = GetTableReader(file_name_);
}

SstFileReader::~SstFileReader() {
}

extern const uint64_t kBlockBasedTableMagicNumber;
extern const uint64_t kLegacyBlockBasedTableMagicNumber;
extern const uint64_t kPlainTableMagicNumber;
extern const uint64_t kLegacyPlainTableMagicNumber;

const char* testFileName = "test_file_name";

Status SstFileReader::GetTableReader(const std::string& file_path) {
  uint64_t magic_number;

  // read table magic number
  Footer footer;

  unique_ptr<RandomAccessFile> file;
  uint64_t file_size;
  Status s = options_.env->NewRandomAccessFile(file_path, &file, soptions_);
  if (s.ok()) {
    s = options_.env->GetFileSize(file_path, &file_size);
  }

  file_.reset(new RandomAccessFileReader(std::move(file)));

  if (s.ok()) {
    s = ReadFooterFromFile(file_.get(), file_size, &footer);
  }
  if (s.ok()) {
    magic_number = footer.table_magic_number();
  }

  if (s.ok()) {
    if (magic_number == kPlainTableMagicNumber ||
        magic_number == kLegacyPlainTableMagicNumber) {
      soptions_.use_mmap_reads = true;
      RETURN_NOT_OK(options_.env->NewRandomAccessFile(file_path, &file, soptions_));
      file_.reset(new RandomAccessFileReader(std::move(file)));
    }
    options_.comparator = internal_comparator_.get();
    // For old sst format, ReadTableProperties might fail but file can be read
    if (ReadTableProperties(magic_number, file_.get(), file_size).ok()) {
      RETURN_NOT_OK(SetTableOptionsByMagicNumber(magic_number));
    } else {
      RETURN_NOT_OK(SetOldTableOptions());
    }
  }

  if (s.ok()) {
    s = NewTableReader(ioptions_, soptions_, *internal_comparator_, file_size,
                       &table_reader_);
    if (s.ok() && table_reader_->IsSplitSst()) {
      unique_ptr<RandomAccessFile> data_file;
      RETURN_NOT_OK(options_.env->NewRandomAccessFile(
          TableBaseToDataFileName(file_path), &data_file, soptions_));
      unique_ptr<RandomAccessFileReader> data_file_reader(
          new RandomAccessFileReader(std::move(data_file)));
      table_reader_->SetDataFileReader(std::move(data_file_reader));
    }
  }
  return s;
}

Status SstFileReader::NewTableReader(
    const ImmutableCFOptions& ioptions, const EnvOptions& soptions,
    const InternalKeyComparator& internal_comparator, uint64_t file_size,
    unique_ptr<TableReader>* table_reader) {
  // We need to turn off pre-fetching of index and filter nodes for
  // BlockBasedTable
  shared_ptr<BlockBasedTableFactory> block_table_factory =
      dynamic_pointer_cast<BlockBasedTableFactory>(options_.table_factory);

  if (block_table_factory) {
    return block_table_factory->NewTableReader(
        TableReaderOptions(ioptions_, soptions_, internal_comparator_,
                           /*skip_filters=*/false),
        std::move(file_), file_size, &table_reader_, DataIndexLoadMode::USE_CACHE,
        PrefetchFilter::NO);
  }

  assert(!block_table_factory);

  // For all other factory implementation
  return options_.table_factory->NewTableReader(
      TableReaderOptions(ioptions_, soptions_, internal_comparator_),
      std::move(file_), file_size, &table_reader_);
}

Status SstFileReader::DumpTable(const std::string& out_filename) {
  unique_ptr<WritableFile> out_file;
  Env* env = Env::Default();
  RETURN_NOT_OK(env->NewWritableFile(out_filename, &out_file, soptions_));
  Status s = table_reader_->DumpTable(out_file.get());
  RETURN_NOT_OK(out_file->Close());
  return s;
}

uint64_t SstFileReader::CalculateCompressedTableSize(
    const TableBuilderOptions& tb_options, size_t block_size) {
  unique_ptr<WritableFile> out_file;
  unique_ptr<Env> env(NewMemEnv(Env::Default()));
  CHECK_OK(env->NewWritableFile(testFileName, &out_file, soptions_));
  unique_ptr<WritableFileWriter> dest_writer;
  dest_writer.reset(new WritableFileWriter(std::move(out_file), soptions_));
  BlockBasedTableOptions table_options;
  table_options.block_size = block_size;
  BlockBasedTableFactory block_based_tf(table_options);
  unique_ptr<TableBuilder> table_builder;
  table_builder = block_based_tf.NewTableBuilder(
      tb_options,
      TablePropertiesCollectorFactory::Context::kUnknownColumnFamily,
      dest_writer.get());
  unique_ptr<InternalIterator> iter(table_reader_->NewIterator(ReadOptions()));
  for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
    if (!iter->status().ok()) {
      fputs(iter->status().ToString().c_str(), stderr);
      exit(1);
    }
    table_builder->Add(iter->key(), iter->value());
  }
  Status s = table_builder->Finish();
  if (!s.ok()) {
    fputs(s.ToString().c_str(), stderr);
    exit(1);
  }
  uint64_t size = table_builder->TotalFileSize();
  CHECK_OK(env->DeleteFile(testFileName));
  return size;
}

int SstFileReader::ShowAllCompressionSizes(size_t block_size) {
  ReadOptions read_options;
  Options opts;
  const ImmutableCFOptions imoptions(opts);
  auto ikc = std::make_shared<rocksdb::InternalKeyComparator>(opts.comparator);
  std::vector<std::unique_ptr<IntTblPropCollectorFactory> >
      block_based_table_factories;

  std::map<CompressionType, const char*> compress_type;
  compress_type.insert(
      std::make_pair(CompressionType::kNoCompression, "kNoCompression"));
  compress_type.insert(std::make_pair(CompressionType::kSnappyCompression,
                                      "kSnappyCompression"));
  compress_type.insert(
      std::make_pair(CompressionType::kZlibCompression, "kZlibCompression"));
  compress_type.insert(
      std::make_pair(CompressionType::kBZip2Compression, "kBZip2Compression"));
  compress_type.insert(
      std::make_pair(CompressionType::kLZ4Compression, "kLZ4Compression"));
  compress_type.insert(
      std::make_pair(CompressionType::kLZ4HCCompression, "kLZ4HCCompression"));
  compress_type.insert(std::make_pair(CompressionType::kZSTDNotFinalCompression,
                                      "kZSTDNotFinalCompression"));

  fprintf(stdout, "Block Size: %" ROCKSDB_PRIszt "\n", block_size);

  for (CompressionType i = CompressionType::kNoCompression;
       i <= CompressionType::kZSTDNotFinalCompression;
       i = (i == kLZ4HCCompression) ? kZSTDNotFinalCompression
                                    : CompressionType(i + 1)) {
    CompressionOptions compress_opt;
    TableBuilderOptions tb_opts(imoptions,
                                ikc,
                                block_based_table_factories,
                                i,
                                compress_opt,
                                false);
    uint64_t file_size = CalculateCompressedTableSize(tb_opts, block_size);
    fprintf(stdout, "Compression: %s", compress_type.find(i)->second);
    fprintf(stdout, " Size: %" PRIu64 "\n", file_size);
  }
  return 0;
}

Status SstFileReader::ReadTableProperties(uint64_t table_magic_number,
                                          RandomAccessFileReader* file,
                                          uint64_t file_size) {
  TableProperties* table_properties = nullptr;
  Status s = rocksdb::ReadTableProperties(file, file_size, table_magic_number,
                                          options_.env, options_.info_log.get(),
                                          &table_properties);
  if (s.ok()) {
    table_properties_.reset(table_properties);
  } else {
    fprintf(stdout, "Not able to read table properties\n");
  }
  return s;
}

Status SstFileReader::SetTableOptionsByMagicNumber(
    uint64_t table_magic_number) {
  assert(table_properties_);
  if (table_magic_number == kBlockBasedTableMagicNumber ||
      table_magic_number == kLegacyBlockBasedTableMagicNumber) {
    options_.table_factory = std::make_shared<BlockBasedTableFactory>();
    fprintf(stdout, "Sst file format: block-based\n");
    auto& props = table_properties_->user_collected_properties;
    auto pos = props.find(BlockBasedTablePropertyNames::kIndexType);
    if (pos != props.end()) {
      auto index_type_on_file = static_cast<IndexType>(DecodeFixed32(pos->second.c_str()));
      if (index_type_on_file == IndexType::kHashSearch) {
        options_.prefix_extractor.reset(NewNoopTransform());
      }
    }
  } else if (table_magic_number == kPlainTableMagicNumber ||
             table_magic_number == kLegacyPlainTableMagicNumber) {
    options_.allow_mmap_reads = true;

    PlainTableOptions plain_table_options;
    plain_table_options.user_key_len = kPlainTableVariableLength;
    plain_table_options.bloom_bits_per_key = 0;
    plain_table_options.hash_table_ratio = 0;
    plain_table_options.index_sparseness = 1;
    plain_table_options.huge_page_tlb_size = 0;
    plain_table_options.encoding_type = kPlain;
    plain_table_options.full_scan_mode = true;

    options_.table_factory.reset(NewPlainTableFactory(plain_table_options));
    fprintf(stdout, "Sst file format: plain table\n");
  } else {
    char error_msg_buffer[80];
    snprintf(error_msg_buffer, sizeof(error_msg_buffer) - 1,
             "Unsupported table magic number --- %" PRIx64, table_magic_number);
    return STATUS(InvalidArgument, error_msg_buffer);
  }

  return Status::OK();
}

Status SstFileReader::SetOldTableOptions() {
  assert(table_properties_ == nullptr);
  options_.table_factory = std::make_shared<BlockBasedTableFactory>();
  fprintf(stdout, "Sst file format: block-based(old version)\n");

  return Status::OK();
}

Status SstFileReader::ReadSequential(bool print_kv,
                                     uint64_t read_num,
                                     bool has_from,
                                     const std::string& from_key,
                                     bool has_to,
                                     const std::string& to_key) {
  if (!table_reader_) {
    return init_result_;
  }

  InternalIterator* iter =
      table_reader_->NewIterator(ReadOptions(verify_checksum_, false));
  uint64_t i = 0;
  if (has_from) {
    InternalKey ikey = InternalKey::MaxPossibleForUserKey(from_key);
    iter->Seek(ikey.Encode());
  } else {
    iter->SeekToFirst();
  }
  for (; iter->Valid(); iter->Next()) {
    Slice key = iter->key();
    Slice value = iter->value();
    ++i;
    if (read_num > 0 && i > read_num)
      break;

    ParsedInternalKey ikey;
    if (!ParseInternalKey(key, &ikey)) {
      std::cerr << "Internal Key ["
                << key.ToString(true /* in hex*/)
                << "] parse error!\n";
      continue;
    }

    // If end marker was specified, we stop before it
    if (has_to && BytewiseComparator()->Compare(ikey.user_key, to_key) >= 0) {
      break;
    }

    if (print_kv) {
      switch (output_format_) {
        case OutputFormat::kRaw:
        case OutputFormat::kHex: {
          bool output_hex = (output_format_ == OutputFormat::kHex);
          fprintf(
              stdout, "%s => %s\n", ikey.DebugString(output_hex).c_str(),
              value.ToString(output_hex).c_str());
          break;
        }
        case OutputFormat::kDecodedRegularDB:
        case OutputFormat::kDecodedIntentsDB:
          auto storage_type =
              (output_format_ == OutputFormat::kDecodedRegularDB ? StorageDbType::kRegular
                                                                 : StorageDbType::kIntents);
          fprintf(stdout, "%s\n", docdb_kv_formatter_->Format(key, value, storage_type).c_str());
          break;
      }
    }
  }

  read_num_ += i;

  Status ret = iter->status();
  delete iter;
  return ret;
}

Status SstFileReader::ReadTableProperties(
    std::shared_ptr<const TableProperties>* table_properties) {
  if (!table_reader_) {
    return init_result_;
  }

  *table_properties = table_reader_->GetTableProperties();
  return init_result_;
}

namespace {

void print_help() {
  fprintf(stderr,
          "sst_dump [--command=check|scan|none|raw] [--verify_checksum] "
          "--file=data_dir_OR_sst_file"
          " [--output_format=raw|hex|decoded_regulardb|decoded_intentsdb]"
          " [--formatter_tablet_metadata=<path_to_tablet_metadata>"
          " [--input_key_hex]"
          " [--from=<user_key>]"
          " [--to=<user_key>]"
          " [--read_num=NUM]"
          " [--show_properties]"
          " [--show_compression_sizes]"
          " [--show_compression_sizes [--set_block_size=<block_size>]]\n");
}

}  // namespace

int SSTDumpTool::Run(int argc, char** argv) {
  const char* dir_or_file = nullptr;
  uint64_t read_num = -1;
  std::string command;

  char junk;
  uint64_t n;
  bool verify_checksum = false;
  OutputFormat output_format = OutputFormat::kRaw;
  bool input_key_hex = false;
  bool has_from = false;
  bool has_to = false;
  bool show_properties = false;
  bool show_compression_sizes = false;
  bool set_block_size = false;
  std::string from_key;
  std::string to_key;
  std::string block_size_str;
  size_t block_size;
  for (int i = 1; i < argc; i++) {
    if (strncmp(argv[i], "--file=", 7) == 0) {
      dir_or_file = argv[i] + 7;
    } else if (strncmp(argv[i], "--output_format=", 16) == 0) {
      auto option = argv[i] + 16;
      if (strcmp(option, "raw") == 0) {
        output_format = OutputFormat::kRaw;
      } else if (strcmp(option, "hex") == 0) {
        output_format = OutputFormat::kHex;
      } else if (strcmp(option, "decoded_regulardb") == 0) {
        output_format = OutputFormat::kDecodedRegularDB;
      } else if (strcmp(option, "decoded_intentsdb") == 0) {
        output_format = OutputFormat::kDecodedIntentsDB;
      } else {
        print_help();
        exit(1);
      }
    } else if (strcmp(argv[i], "--input_key_hex") == 0) {
      input_key_hex = true;
    } else if (sscanf(argv[i], "--read_num=%" PRIu64 "%c", &n, &junk) == 1) {
      read_num = n;
    } else if (strcmp(argv[i], "--verify_checksum") == 0) {
      verify_checksum = true;
    } else if (strncmp(argv[i], "--command=", 10) == 0) {
      command = argv[i] + 10;
    } else if (strncmp(argv[i], "--from=", 7) == 0) {
      from_key = argv[i] + 7;
      has_from = true;
    } else if (strncmp(argv[i], "--to=", 5) == 0) {
      to_key = argv[i] + 5;
      has_to = true;
    } else if (strcmp(argv[i], "--show_properties") == 0) {
      show_properties = true;
    } else if (strcmp(argv[i], "--show_compression_sizes") == 0) {
      show_compression_sizes = true;
    } else if (strncmp(argv[i], "--set_block_size=", 17) == 0) {
      set_block_size = true;
      block_size_str = argv[i] + 17;
      std::istringstream iss(block_size_str);
      if (iss.fail()) {
        fprintf(stderr, "block size must be numeric\n");
        exit(1);
      }
      iss >> block_size;
    } else if (strncmp(argv[i], "--formatter_", 12) == 0) {
      auto status = formatter_->ProcessArgument(argv[i] + 12);
      if (!status.ok()) {
        fprintf(stderr, "%s\n", status.ToString().c_str());
        exit(1);
      }
    } else {
      print_help();
      exit(1);
    }
  }

  if (input_key_hex) {
    if (has_from) {
      from_key = rocksdb::LDBCommand::HexToString(from_key);
    }
    if (has_to) {
      to_key = rocksdb::LDBCommand::HexToString(to_key);
    }
  }

  if (dir_or_file == nullptr) {
    print_help();
    exit(1);
  }

  std::vector<std::string> filenames;
  rocksdb::Env* env = rocksdb::Env::Default();
  rocksdb::Status st = env->GetChildren(dir_or_file, &filenames);
  bool dir = true;
  if (!st.ok()) {
    filenames.clear();
    filenames.push_back(dir_or_file);
    dir = false;
  }

  fprintf(stdout, "from [%s] to [%s]\n",
      rocksdb::Slice(from_key).ToString(true).c_str(),
      rocksdb::Slice(to_key).ToString(true).c_str());

  uint64_t total_read = 0;
  for (size_t i = 0; i < filenames.size(); i++) {
    std::string filename = filenames.at(i);
    if (filename.length() <= 4 ||
        filename.rfind(".sst") != filename.length() - 4) {
      // ignore
      continue;
    }
    if (dir) {
      filename = std::string(dir_or_file) + "/" + filename;
    }

    rocksdb::SstFileReader reader(filename, verify_checksum, output_format, formatter_);
    if (!reader.getStatus().ok()) {
      fprintf(stderr, "%s: %s\n", filename.c_str(),
              reader.getStatus().ToString().c_str());
      exit(1);
    }

    if (show_compression_sizes) {
      if (set_block_size) {
        reader.ShowAllCompressionSizes(block_size);
      } else {
        reader.ShowAllCompressionSizes(16384);
      }
      return 0;
    }

    if (command == "raw") {
      std::string out_filename = filename.substr(0, filename.length() - 4);
      out_filename.append("_dump.txt");

      st = reader.DumpTable(out_filename);
      if (!st.ok()) {
        fprintf(stderr, "%s: %s\n", filename.c_str(), st.ToString().c_str());
        exit(1);
      } else {
        fprintf(stdout, "raw dump written to file %s\n", &out_filename[0]);
      }
      continue;
    }

    // scan all files in give file path.
    if (command == "" || command == "scan" || command == "check") {
      st = reader.ReadSequential(command == "scan",
                                 read_num > 0 ? (read_num - total_read) :
                                                read_num,
                                 has_from, from_key, has_to, to_key);
      if (!st.ok()) {
        fprintf(stderr, "%s: %s\n", filename.c_str(),
            st.ToString().c_str());
      }
      total_read += reader.GetReadNumber();
      if (read_num > 0 && total_read > read_num) {
        break;
      }
    }
    if (show_properties) {
      const rocksdb::TableProperties* table_properties;

      std::shared_ptr<const rocksdb::TableProperties>
          table_properties_from_reader;
      st = reader.ReadTableProperties(&table_properties_from_reader);
      if (!st.ok()) {
        fprintf(stderr, "%s: %s\n", filename.c_str(), st.ToString().c_str());
        fprintf(stderr, "Try to use initial table properties\n");
        table_properties = reader.GetInitTableProperties();
      } else {
        table_properties = table_properties_from_reader.get();
      }
      if (table_properties != nullptr) {
        fprintf(stdout,
                "Table Properties:\n"
                "------------------------------\n"
                "  %s",
                table_properties->ToString("\n  ", ": ").c_str());
        fprintf(stdout, "# deleted keys: %" PRIu64 "\n",
                rocksdb::GetDeletedKeys(
                    table_properties->user_collected_properties));
        fprintf(stdout,
                "  User collected properties:\n"
                "  ------------------------------\n");
        for (const auto& prop : table_properties->user_collected_properties) {
          fprintf(
              stdout, "  %s: %s\n", prop.first.c_str(), Slice(prop.second).ToDebugString().c_str());
        }
      }
    }
  }
  return 0;
}
}  // namespace rocksdb
