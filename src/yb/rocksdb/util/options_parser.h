// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
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


#pragma once

#include <map>
#include <string>
#include <vector>

#include "yb/rocksdb/env.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/table/block_based_table_factory.h"
#include "yb/rocksdb/util/options_sanity_check.h"

namespace rocksdb {

#define ROCKSDB_OPTION_FILE_MAJOR 1
#define ROCKSDB_OPTION_FILE_MINOR 1

enum OptionSection : char {
  kOptionSectionVersion = 0,
  kOptionSectionDBOptions,
  kOptionSectionCFOptions,
  kOptionSectionTableOptions,
  kOptionSectionUnknown
};

static const std::string opt_section_titles[] = {
    "Version", "DBOptions", "CFOptions", "TableOptions/", "Unknown"};

YB_STRONGLY_TYPED_BOOL(IncludeHeader);
YB_STRONGLY_TYPED_BOOL(IncludeFileVersion);

Status PersistRocksDBOptions(const DBOptions& db_opt,
                             const std::vector<std::string>& cf_names,
                             const std::vector<ColumnFamilyOptions>& cf_opts,
                             const std::string& file_name, Env* env,
                             IncludeHeader include_header = IncludeHeader::kTrue,
                             IncludeFileVersion include_file_version = IncludeFileVersion::kTrue);

class RocksDBOptionsParser {
 public:
  RocksDBOptionsParser();
  ~RocksDBOptionsParser() {}
  void Reset();

  Status Parse(const std::string& file_name, Env* env);
  static std::string TrimAndRemoveComment(const std::string& line,
                                          const bool trim_only = false);

  const DBOptions* db_opt() const { return &db_opt_; }
  const std::unordered_map<std::string, std::string>* db_opt_map() const {
    return &db_opt_map_;
  }
  const std::vector<ColumnFamilyOptions>* cf_opts() const { return &cf_opts_; }
  const std::vector<std::string>* cf_names() const { return &cf_names_; }
  const std::vector<std::unordered_map<std::string, std::string>>* cf_opt_maps()
      const {
    return &cf_opt_maps_;
  }

  const ColumnFamilyOptions* GetCFOptions(const std::string& name) {
    return GetCFOptionsImpl(name);
  }
  size_t NumColumnFamilies() { return cf_opts_.size(); }

  static Status VerifyRocksDBOptionsFromFile(
      const DBOptions& db_opt, const std::vector<std::string>& cf_names,
      const std::vector<ColumnFamilyOptions>& cf_opts,
      const std::string& file_name, Env* env,
      OptionsSanityCheckLevel sanity_check_level = kSanityLevelExactMatch);

  static Status VerifyDBOptions(
      const DBOptions& base_opt, const DBOptions& new_opt,
      const std::unordered_map<std::string, std::string>* new_opt_map = nullptr,
      OptionsSanityCheckLevel sanity_check_level = kSanityLevelExactMatch);

  static Status VerifyCFOptions(
      const ColumnFamilyOptions& base_opt, const ColumnFamilyOptions& new_opt,
      const std::unordered_map<std::string, std::string>* new_opt_map = nullptr,
      OptionsSanityCheckLevel sanity_check_level = kSanityLevelExactMatch);

  static Status VerifyTableFactory(
      const TableFactory* base_tf, const TableFactory* file_tf,
      OptionsSanityCheckLevel sanity_check_level = kSanityLevelExactMatch);

  static Status VerifyBlockBasedTableFactory(
      const BlockBasedTableFactory* base_tf,
      const BlockBasedTableFactory* file_tf,
      OptionsSanityCheckLevel sanity_check_level);

  static Status ExtraParserCheck(const RocksDBOptionsParser& input_parser);

 protected:
  bool IsSection(const std::string& line);
  Status ParseSection(OptionSection* section, std::string* title,
                      std::string* argument, const std::string& line,
                      const int line_num);

  Status CheckSection(const OptionSection section,
                      const std::string& section_arg, const int line_num);

  Status ParseStatement(std::string* name, std::string* value,
                        const std::string& line, const int line_num);

  Status EndSection(
      const OptionSection section, const std::string& title,
      const std::string& section_arg,
      const std::unordered_map<std::string, std::string>& opt_map);

  Status ValidityCheck();

  Status InvalidArgument(const int line_num, const std::string& message);

  Status ParseVersionNumber(const std::string& ver_name,
                            const std::string& ver_string, const int max_count,
                            int* version);

  ColumnFamilyOptions* GetCFOptionsImpl(const std::string& name) {
    assert(cf_names_.size() == cf_opts_.size());
    for (size_t i = 0; i < cf_names_.size(); ++i) {
      if (cf_names_[i] == name) {
        return &cf_opts_[i];
      }
    }
    return nullptr;
  }

 private:
  DBOptions db_opt_;
  std::unordered_map<std::string, std::string> db_opt_map_;
  std::vector<std::string> cf_names_;
  std::vector<ColumnFamilyOptions> cf_opts_;
  std::vector<std::unordered_map<std::string, std::string>> cf_opt_maps_;
  bool has_version_section_;
  bool has_db_options_;
  bool has_default_cf_options_;
  int db_version[3];
  int opt_file_version[3];
};

}  // namespace rocksdb
