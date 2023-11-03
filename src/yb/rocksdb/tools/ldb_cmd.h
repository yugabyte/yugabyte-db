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

#pragma once


#include <stdlib.h>
#include <stdio.h>

#include <string>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <vector>
#include <map>

#include "yb/encryption/header_manager_impl.h"
#include "yb/encryption/universe_key_manager.h"

#include "yb/rocksdb/db/version_set.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/iterator.h"
#include "yb/rocksdb/ldb_tool.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/tools/ldb_cmd_execute_result.h"
#include "yb/rocksdb/util/logging.h"
#include "yb/util/slice.h"
#include "yb/util/string_util.h"
#include "yb/rocksutil/rocksdb_encrypted_file_factory.h"


namespace rocksdb {

class LDBCommand {
 public:
  // Command-line arguments
  static const std::string ARG_DB;
  static const std::string ARG_PATH;
  static const std::string ARG_HEX;
  static const std::string ARG_KEY_HEX;
  static const std::string ARG_VALUE_HEX;
  static const std::string ARG_CF_NAME;
  static const std::string ARG_FROM;
  static const std::string ARG_TO;
  static const std::string ARG_MAX_KEYS;
  static const std::string ARG_BLOOM_BITS;
  static const std::string ARG_FIX_PREFIX_LEN;
  static const std::string ARG_COMPRESSION_TYPE;
  static const std::string ARG_BLOCK_SIZE;
  static const std::string ARG_AUTO_COMPACTION;
  static const std::string ARG_DB_WRITE_BUFFER_SIZE;
  static const std::string ARG_WRITE_BUFFER_SIZE;
  static const std::string ARG_FILE_SIZE;
  static const std::string ARG_CREATE_IF_MISSING;
  static const std::string ARG_NO_VALUE;
  static const std::string ARG_UNIVERSE_KEY_FILE;
  static const std::string ARG_ONLY_VERIFY_CHECKSUMS;

  static LDBCommand* InitFromCmdLineArgs(
      const std::vector<std::string>& args, const Options& options,
      const LDBOptions& ldb_options,
      const std::vector<ColumnFamilyDescriptor>* column_families);

  static LDBCommand* InitFromCmdLineArgs(
      int argc, char** argv, const Options& options,
      const LDBOptions& ldb_options,
      const std::vector<ColumnFamilyDescriptor>* column_families);

  bool ValidateCmdLineOptions();

  virtual Options PrepareOptionsForOpenDB();

  virtual void SetDBOptions(Options options) {
    options_ = options;
  }

  virtual void SetColumnFamilies(
      const std::vector<ColumnFamilyDescriptor>* column_families) {
    if (column_families != nullptr) {
      column_families_ = *column_families;
    } else {
      column_families_.clear();
    }
  }

  void SetLDBOptions(const LDBOptions& ldb_options) {
    ldb_options_ = ldb_options;
  }

  virtual bool NoDBOpen() {
    return false;
  }

  virtual ~LDBCommand() { CloseDB(); }

  /* Run the command, and return the execute result. */
  void Run() {
    if (!exec_state_.IsNotStarted()) {
      return;
    }

    if (db_ == nullptr && !NoDBOpen()) {
      OpenDB();
    }

    // We'll intentionally proceed even if the DB can't be opened because users
    // can also specify a filename, not just a directory.
    DoCommand();

    if (exec_state_.IsNotStarted()) {
      exec_state_ = LDBCommandExecuteResult::Succeed("");
    }

    if (db_ != nullptr) {
      CloseDB ();
    }
  }

  virtual void DoCommand() = 0;

  LDBCommandExecuteResult GetExecuteState() {
    return exec_state_;
  }

  void ClearPreviousRunState() {
    exec_state_.Reset();
  }

  static std::string HexToString(const std::string& str) {
    std::string::size_type len = str.length();
    std::string parsed;
    static const char* const hexas = "0123456789ABCDEF";
    parsed.reserve(len / 2);

    if (len < 2 || str[0] != '0' || str[1] != 'x') {
      fprintf(stderr, "Invalid hex input %s.  Must start with 0x\n",
              str.c_str());
      throw "Invalid hex input";
    }

    for (unsigned int i = 2; i < len; i += 2) {
      char a = static_cast<char>(toupper(str[i]));
      const char* p = std::lower_bound(hexas, hexas + 16, a);
      if (*p != a) {
        throw "Invalid hex value";
      }

      if (i + 1 >= len) {
        // if odd number of chars than we just hit end of string
        parsed.push_back(static_cast<char>(p - hexas));
        break;
      }

      char b = static_cast<char>(toupper(str[i + 1]));
      const char* q = std::lower_bound(hexas, hexas + 16, b);
      if (*q == b) {
        // pairwise compute decimal value from hex
        parsed.push_back(static_cast<char>(((p - hexas) << 4) | (q - hexas)));
      } else {
        throw "Invalid hex value";
      }
    }
    return parsed;
  }

  static std::string StringToHex(const std::string& str) {
    std::string result = "0x";
    char buf[10];
    for (size_t i = 0; i < str.length(); i++) {
      snprintf(buf, sizeof(buf), "%02X", (unsigned char)str[i]);
      result += buf;
    }
    return result;
  }

  static const char* DELIM;

 protected:

  LDBCommandExecuteResult exec_state_;
  std::string db_path_;
  std::string column_family_name_;
  DB* db_;
  std::map<std::string, ColumnFamilyHandle*> cf_handles_;

  /**
   * true implies that this command can work if the db is opened in read-only
   * mode.
   */
  bool is_read_only_;

  /** If true, the key is input/output as hex in get/put/scan/delete etc. */
  bool is_key_hex_;

  /** If true, the value is input/output as hex in get/put/scan/delete etc. */
  bool is_value_hex_;

  /**
   * Map of options passed on the command-line.
   */
  const std::map<std::string, std::string> option_map_;

  /**
   * Flags passed on the command-line.
   */
  const std::vector<std::string> flags_;

  /** List of command-line options valid for this command */
  const std::vector<std::string> valid_cmd_line_options_;

  std::unique_ptr<yb::encryption::UniverseKeyManager> universe_key_manager_;
  std::unique_ptr<rocksdb::Env> env_;

  bool ParseKeyValue(const std::string& line, std::string* key, std::string* value,
                      bool is_key_hex, bool is_value_hex);

  LDBCommand(
      const std::map<std::string, std::string>& options,
      const std::vector<std::string>& flags,
      bool is_read_only,
      const std::vector<std::string>& valid_cmd_line_options)
      : db_(nullptr),
        is_read_only_(is_read_only),
        is_key_hex_(false),
        is_value_hex_(false),
        option_map_(options),
        flags_(flags),
        valid_cmd_line_options_(valid_cmd_line_options) {

    std::map<std::string, std::string>::const_iterator itr = options.find(ARG_DB);
    if (itr != options.end()) {
      db_path_ = itr->second;
    }

    itr = options.find(ARG_UNIVERSE_KEY_FILE);
    if (itr != options.end()) {
      std::vector<std::string> splits = StringSplit(itr->second, ':');
      if (splits.size() != 2) {
        LOG(FATAL) << yb::Format("Could not split $0 by ':' into a key id and key file",
                                 itr->second);
      }
      std::string key_data;
      auto key_id = splits[0];
      auto key_path = splits[1];
      Status s = ReadFileToString(Env::Default(), key_path, &key_data);
      if(!s.ok()) {
        LOG(FATAL) << yb::Format("Could not read file at path $0: $1", key_path, s.ToString());
      }
      auto res = yb::encryption::UniverseKeyManager::FromKey(key_id, yb::Slice(key_data));
      if (!res.ok()) {
        LOG(FATAL) << "Could not create universe key manager: " << res.status().ToString();
      }
      universe_key_manager_ = std::move(*res);
      env_ = yb::NewRocksDBEncryptedEnv(
          yb::encryption::DefaultHeaderManager(universe_key_manager_.get()));
    }

    itr = options.find(ARG_CF_NAME);
    if (itr != options.end()) {
      column_family_name_ = itr->second;
    } else {
      column_family_name_ = kDefaultColumnFamilyName;
    }

    is_key_hex_ = IsKeyHex(options, flags);
    is_value_hex_ = IsValueHex(options, flags);
  }

  void OpenDB() {
    Options opt = PrepareOptionsForOpenDB();
    if (!exec_state_.IsNotStarted()) {
      return;
    }
    // Open the DB.
    Status st;
    std::vector<ColumnFamilyHandle*> handles_opened;
    if (column_families_.empty()) {
      // Try to figure out column family lists
      std::vector<std::string> cf_list;
      st = DB::ListColumnFamilies(DBOptions(), db_path_, &cf_list);
      // There is possible the DB doesn't exist yet, for "create if not
      // "existing case". The failure is ignored here. We rely on DB::Open()
      // to give us the correct error message for problem with opening
      // existing DB.
      if (st.ok() && cf_list.size() > 1) {
        // Ignore single column family DB.
        for (auto cf_name : cf_list) {
          column_families_.emplace_back(cf_name, opt);
        }
      }
    }
    if (is_read_only_) {
      if (column_families_.empty()) {
        st = DB::OpenForReadOnly(opt, db_path_, &db_);
      } else {
        st = DB::OpenForReadOnly(opt, db_path_, column_families_,
                                  &handles_opened, &db_);
      }
    } else {
      if (column_families_.empty()) {
        st = DB::Open(opt, db_path_, &db_);
      } else {
        st = DB::Open(opt, db_path_, column_families_, &handles_opened, &db_);
      }
    }
    if (!st.ok()) {
      std::string msg = st.ToString();
      exec_state_ = LDBCommandExecuteResult::Failed(msg);
    } else if (!handles_opened.empty()) {
      assert(handles_opened.size() == column_families_.size());
      bool found_cf_name = false;
      for (size_t i = 0; i < handles_opened.size(); i++) {
        cf_handles_[column_families_[i].name] = handles_opened[i];
        if (column_family_name_ == column_families_[i].name) {
          found_cf_name = true;
        }
      }
      if (!found_cf_name) {
        exec_state_ = LDBCommandExecuteResult::Failed(
            "Non-existing column family " + column_family_name_);
        CloseDB();
      }
    } else {
      // We successfully opened DB in single column family mode.
      assert(column_families_.empty());
      if (column_family_name_ != kDefaultColumnFamilyName) {
        exec_state_ = LDBCommandExecuteResult::Failed(
            "Non-existing column family " + column_family_name_);
        CloseDB();
      }
    }

    options_ = opt;
  }

  void CloseDB () {
    if (db_ != nullptr) {
      for (auto& pair : cf_handles_) {
        delete pair.second;
      }
      delete db_;
      db_ = nullptr;
    }
  }

  ColumnFamilyHandle* GetCfHandle() {
    if (!cf_handles_.empty()) {
      auto it = cf_handles_.find(column_family_name_);
      if (it == cf_handles_.end()) {
        exec_state_ = LDBCommandExecuteResult::Failed(
            "Cannot find column family " + column_family_name_);
      } else {
        return it->second;
      }
    }
    return db_->DefaultColumnFamily();
  }

  static std::string PrintKeyValue(const std::string& key, const std::string& value,
        bool is_key_hex, bool is_value_hex) {
    std::string result;
    result.append(is_key_hex ? StringToHex(key) : key);
    result.append(DELIM);
    result.append(is_value_hex ? StringToHex(value) : value);
    return result;
  }

  static std::string PrintKeyValue(const std::string& key, const std::string& value,
        bool is_hex) {
    return PrintKeyValue(key, value, is_hex, is_hex);
  }

  /**
   * Return true if the specified flag is present in the specified flags vector
   */
  static bool IsFlagPresent(const std::vector<std::string>& flags, const std::string& flag) {
    return (std::find(flags.begin(), flags.end(), flag) != flags.end());
  }

  static std::string HelpRangeCmdArgs() {
    std::ostringstream str_stream;
    str_stream << " ";
    str_stream << "[--" << ARG_FROM << "] ";
    str_stream << "[--" << ARG_TO << "] ";
    return str_stream.str();
  }

  /**
   * A helper function that returns a list of command line options
   * used by this command.  It includes the common options and the ones
   * passed in.
   */
  static std::vector<std::string> BuildCmdLineOptions(std::vector<std::string> options) {
    std::vector<std::string> ret = {ARG_DB, ARG_BLOOM_BITS, ARG_BLOCK_SIZE,
                          ARG_AUTO_COMPACTION, ARG_COMPRESSION_TYPE,
                          ARG_WRITE_BUFFER_SIZE, ARG_FILE_SIZE,
                          ARG_FIX_PREFIX_LEN, ARG_CF_NAME, ARG_UNIVERSE_KEY_FILE};
    ret.insert(ret.end(), options.begin(), options.end());
    return ret;
  }

  bool ParseIntOption(const std::map<std::string, std::string>& options, const std::string& option,
                      int& value, LDBCommandExecuteResult& exec_state); // NOLINT

  bool ParseStringOption(const std::map<std::string, std::string>& options,
                         const std::string& option, std::string* value);

  Options options_;
  std::vector<ColumnFamilyDescriptor> column_families_;
  LDBOptions ldb_options_;

 private:

  /**
   * Interpret command line options and flags to determine if the key
   * should be input/output in hex.
   */
  bool IsKeyHex(const std::map<std::string, std::string>& options,
      const std::vector<std::string>& flags) {
    return (IsFlagPresent(flags, ARG_HEX) ||
        IsFlagPresent(flags, ARG_KEY_HEX) ||
        ParseBooleanOption(options, ARG_HEX, false) ||
        ParseBooleanOption(options, ARG_KEY_HEX, false));
  }

  /**
   * Interpret command line options and flags to determine if the value
   * should be input/output in hex.
   */
  bool IsValueHex(const std::map<std::string, std::string>& options,
      const std::vector<std::string>& flags) {
    return (IsFlagPresent(flags, ARG_HEX) ||
          IsFlagPresent(flags, ARG_VALUE_HEX) ||
          ParseBooleanOption(options, ARG_HEX, false) ||
          ParseBooleanOption(options, ARG_VALUE_HEX, false));
  }

  /**
   * Returns the value of the specified option as a boolean.
   * default_val is used if the option is not found in options.
   * Throws an exception if the value of the option is not
   * "true" or "false" (case insensitive).
   */
  bool ParseBooleanOption(const std::map<std::string, std::string>& options,
      const std::string& option, bool default_val) {

    std::map<std::string, std::string>::const_iterator itr = options.find(option);
    if (itr != options.end()) {
      std::string option_val = itr->second;
      return StringToBool(itr->second);
    }
    return default_val;
  }

  /**
   * Converts val to a boolean.
   * val must be either true or false (case insensitive).
   * Otherwise an exception is thrown.
   */
  bool StringToBool(std::string val) {
    std::transform(val.begin(), val.end(), val.begin(),
                   [](char ch)->char { return static_cast<char>(::tolower(ch)); });

    if (val == "true") {
      return true;
    } else if (val == "false") {
      return false;
    } else {
      throw "Invalid value for boolean argument";
    }
  }

  static LDBCommand* SelectCommand(
    const std::string& cmd,
    const std::vector<std::string>& cmdParams,
    const std::map<std::string, std::string>& option_map,
    const std::vector<std::string>& flags
  );

};

class CompactorCommand: public LDBCommand {
 public:
  static std::string Name() { return "compact"; }

  CompactorCommand(const std::vector<std::string>& params,
      const std::map<std::string, std::string>& options, const std::vector<std::string>& flags);

  static void Help(std::string& ret); // NOLINT

  virtual void DoCommand() override;

 private:
  bool null_from_;
  std::string from_;
  bool null_to_;
  std::string to_;
};

class DBFileDumperCommand : public LDBCommand {
 public:
  static std::string Name() { return "dump_live_files"; }

  DBFileDumperCommand(const std::vector<std::string>& params,
                      const std::map<std::string, std::string>& options,
                      const std::vector<std::string>& flags);

  static void Help(std::string& ret); // NOLINT

  virtual void DoCommand() override;
};

class DBDumperCommand: public LDBCommand {
 public:
  static std::string Name() { return "dump"; }

  DBDumperCommand(const std::vector<std::string>& params,
      const std::map<std::string, std::string>& options, const std::vector<std::string>& flags);

  static void Help(std::string& ret); // NOLINT

  virtual void DoCommand() override;

 private:
  /**
   * Extract file name from the full path. We handle both the forward slash (/)
   * and backslash (\) to make sure that different OS-s are supported.
  */
  static std::string GetFileNameFromPath(const std::string& s) {
    std::size_t n = s.find_last_of("/\\");

    if (std::string::npos == n) {
      return s;
    } else {
      return s.substr(n + 1);
    }
  }

  void DoDumpCommand();

  bool null_from_;
  std::string from_;
  bool null_to_;
  std::string to_;
  int max_keys_;
  std::string delim_;
  bool count_only_;
  bool count_delim_;
  bool print_stats_;
  std::string path_;

  static const std::string ARG_COUNT_ONLY;
  static const std::string ARG_COUNT_DELIM;
  static const std::string ARG_STATS;
};

class InternalDumpCommand: public LDBCommand {
 public:
  static std::string Name() { return "idump"; }

  InternalDumpCommand(const std::vector<std::string>& params,
                      const std::map<std::string, std::string>& options,
                      const std::vector<std::string>& flags);

  static void Help(std::string& ret); // NOLINT

  virtual void DoCommand() override;

 private:
  bool has_from_;
  std::string from_;
  bool has_to_;
  std::string to_;
  int max_keys_;
  std::string delim_;
  bool count_only_;
  bool count_delim_;
  bool print_stats_;
  bool is_input_key_hex_;

  static const std::string ARG_DELIM;
  static const std::string ARG_COUNT_ONLY;
  static const std::string ARG_COUNT_DELIM;
  static const std::string ARG_STATS;
  static const std::string ARG_INPUT_KEY_HEX;
};

class DBLoaderCommand: public LDBCommand {
 public:
  static std::string Name() { return "load"; }

  DBLoaderCommand(std::string& db_name, std::vector<std::string>& args); // NOLINT

  DBLoaderCommand(const std::vector<std::string>& params,
      const std::map<std::string, std::string>& options, const std::vector<std::string>& flags);

  static void Help(std::string& ret); // NOLINT
  virtual void DoCommand() override;

  virtual Options PrepareOptionsForOpenDB() override;

 private:
  bool create_if_missing_;
  bool disable_wal_;
  bool bulk_load_;
  bool compact_;

  static const std::string ARG_DISABLE_WAL;
  static const std::string ARG_BULK_LOAD;
  static const std::string ARG_COMPACT;
};

class ManifestDumpCommand: public LDBCommand {
 public:
  static std::string Name() { return "manifest_dump"; }

  ManifestDumpCommand(const std::vector<std::string>& params,
      const std::map<std::string, std::string>& options, const std::vector<std::string>& flags);

  static void Help(std::string& ret); // NOLINT
  virtual void DoCommand() override;

  virtual bool NoDBOpen() override { return true; }

 private:
  bool verbose_;
  std::string path_;

  static const std::string ARG_VERBOSE;
  static const std::string ARG_JSON;
  static const std::string ARG_PATH;
};

class ListColumnFamiliesCommand : public LDBCommand {
 public:
  static std::string Name() { return "list_column_families"; }

  ListColumnFamiliesCommand(const std::vector<std::string>& params,
                            const std::map<std::string, std::string>& options,
                            const std::vector<std::string>& flags);

  static void Help(std::string& ret); // NOLINT
  virtual void DoCommand() override;

  virtual bool NoDBOpen() override { return true; }

 private:
  std::string dbname_;
};

class CreateColumnFamilyCommand : public LDBCommand {
 public:
  static std::string Name() { return "create_column_family"; }

  CreateColumnFamilyCommand(const std::vector<std::string>& params,
                            const std::map<std::string, std::string>& options,
                            const std::vector<std::string>& flags);

  static void Help(std::string& ret); // NOLINT
  virtual void DoCommand() override;

  virtual bool NoDBOpen() override { return false; }

 private:
  std::string new_cf_name_;
};

class ReduceDBLevelsCommand : public LDBCommand {
 public:
  static std::string Name() { return "reduce_levels"; }

  ReduceDBLevelsCommand(const std::vector<std::string>& params,
      const std::map<std::string, std::string>& options, const std::vector<std::string>& flags);

  virtual Options PrepareOptionsForOpenDB() override;

  virtual void DoCommand() override;

  virtual bool NoDBOpen() override { return true; }

  static void Help(std::string& msg); // NOLINT

  static std::vector<std::string> PrepareArgs(const std::string& db_path, int new_levels,
      bool print_old_level = false);

 private:
  int old_levels_;
  int new_levels_;
  bool print_old_levels_;

  static const std::string ARG_NEW_LEVELS;
  static const std::string ARG_PRINT_OLD_LEVELS;

  Status GetOldNumOfLevels(Options& opt, int* levels); // NOLINT
};

class ChangeCompactionStyleCommand : public LDBCommand {
 public:
  static std::string Name() { return "change_compaction_style"; }

  ChangeCompactionStyleCommand(const std::vector<std::string>& params,
      const std::map<std::string, std::string>& options, const std::vector<std::string>& flags);

  virtual Options PrepareOptionsForOpenDB() override;

  virtual void DoCommand() override;

  static void Help(std::string& msg); // NOLINT

 private:
  int old_compaction_style_;
  int new_compaction_style_;

  static const std::string ARG_OLD_COMPACTION_STYLE;
  static const std::string ARG_NEW_COMPACTION_STYLE;
};

class WALDumperCommand : public LDBCommand {
 public:
  static std::string Name() { return "dump_wal"; }

  WALDumperCommand(const std::vector<std::string>& params,
      const std::map<std::string, std::string>& options, const std::vector<std::string>& flags);

  virtual bool NoDBOpen() override { return true; }

  static void Help(std::string& ret); // NOLINT
  virtual void DoCommand() override;

 private:
  bool print_header_;
  std::string wal_file_;
  bool print_values_;

  static const std::string ARG_WAL_FILE;
  static const std::string ARG_PRINT_HEADER;
  static const std::string ARG_PRINT_VALUE;
};


class GetCommand : public LDBCommand {
 public:
  static std::string Name() { return "get"; }

  GetCommand(
      const std::vector<std::string>& params,
      const std::map<std::string, std::string>& options,
      const std::vector<std::string>& flags);

  virtual void DoCommand() override;

  static void Help(std::string& ret); // NOLINT

 private:
  std::string key_;
};

class ApproxSizeCommand : public LDBCommand {
 public:
  static std::string Name() { return "approxsize"; }

  ApproxSizeCommand(const std::vector<std::string>& params,
      const std::map<std::string, std::string>& options, const std::vector<std::string>& flags);

  virtual void DoCommand() override;

  static void Help(std::string& ret); // NOLINT

 private:
  std::string start_key_;
  std::string end_key_;
};

class BatchPutCommand : public LDBCommand {
 public:
  static std::string Name() { return "batchput"; }

  BatchPutCommand(const std::vector<std::string>& params,
      const std::map<std::string, std::string>& options, const std::vector<std::string>& flags);

  virtual void DoCommand() override;

  static void Help(std::string& ret); // NOLINT

  virtual Options PrepareOptionsForOpenDB() override;

 private:
  /**
   * The key-values to be inserted.
   */
  std::vector<std::pair<std::string, std::string>> key_values_;
};

class ScanCommand : public LDBCommand {
 public:
  static std::string Name() { return "scan"; }

  ScanCommand(
      const std::vector<std::string>& params,
      const std::map<std::string, std::string>& options,
      const std::vector<std::string>& flags);

  virtual void DoCommand() override;

  static void Help(std::string& ret); // NOLINT

 private:
  std::string start_key_;
  std::string end_key_;
  bool start_key_specified_;
  bool end_key_specified_;
  int max_keys_scanned_;
  bool no_value_;
  bool only_verify_checksums_ = false;
};

class DeleteCommand : public LDBCommand {
 public:
  static std::string Name() { return "delete"; }

  DeleteCommand(const std::vector<std::string>& params,
      const std::map<std::string, std::string>& options, const std::vector<std::string>& flags);

  virtual void DoCommand() override;

  static void Help(std::string& ret); // NOLINT

 private:
  std::string key_;
};

class PutCommand : public LDBCommand {
 public:
  static std::string Name() { return "put"; }

  PutCommand(
      const std::vector<std::string>& params,
      const std::map<std::string, std::string>& options,
      const std::vector<std::string>& flags);

  virtual void DoCommand() override;

  static void Help(std::string& ret); // NOLINT

  virtual Options PrepareOptionsForOpenDB() override;

 private:
  std::string key_;
  std::string value_;
};

/**
 * Command that starts up a REPL shell that allows
 * get/put/delete.
 */
class DBQuerierCommand: public LDBCommand {
 public:
  static std::string Name() { return "query"; }

  DBQuerierCommand(const std::vector<std::string>& params,
      const std::map<std::string, std::string>& options, const std::vector<std::string>& flags);

  static void Help(std::string& ret); // NOLINT

  virtual void DoCommand() override;

 private:
  static const char* HELP_CMD;
  static const char* GET_CMD;
  static const char* PUT_CMD;
  static const char* DELETE_CMD;
};

class CheckConsistencyCommand : public LDBCommand {
 public:
  static std::string Name() { return "checkconsistency"; }

  CheckConsistencyCommand(const std::vector<std::string>& params,
      const std::map<std::string, std::string>& options, const std::vector<std::string>& flags);

  virtual void DoCommand() override;

  virtual bool NoDBOpen() override { return true; }

  static void Help(std::string& ret); // NOLINT
};

} // namespace rocksdb
