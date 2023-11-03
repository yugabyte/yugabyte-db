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
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include "yb/rocksdb/db/filename.h"

#include <inttypes.h>

#include <stdio.h>
#include <vector>
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/util/file_reader_writer.h"
#include "yb/rocksdb/util/logging.h"

#include "yb/util/sync_point.h"
#include "yb/util/test_kill.h"

namespace rocksdb {

static const char kRocksDbTFileExt[] = "sst";
static const char kLevelDbTFileExt[] = "ldb";
static const char kRocksDbTSBlockExtSuffix[] = "sblock";
static const char kRocksDbTSBlockFileExt[] = "sst.sblock.0";

// Given a path, flatten the path name by replacing all chars not in
// {[0-9,a-z,A-Z,-,_,.]} with _. And append '_LOG\0' at the end.
// Return the number of chars stored in dest not including the trailing '\0'.
static size_t GetInfoLogPrefix(const std::string& path, char* dest, int len) {
  const char suffix[] = "_LOG";

  size_t write_idx = 0;
  size_t i = 0;
  size_t src_len = path.size();

  while (i < src_len && write_idx < len - sizeof(suffix)) {
    if ((path[i] >= 'a' && path[i] <= 'z') ||
        (path[i] >= '0' && path[i] <= '9') ||
        (path[i] >= 'A' && path[i] <= 'Z') ||
        path[i] == '-' ||
        path[i] == '.' ||
        path[i] == '_') {
      dest[write_idx++] = path[i];
    } else {
      if (i > 0) {
        dest[write_idx++] = '_';
      }
    }
    i++;
  }
  assert(sizeof(suffix) <= len - write_idx);
  // "\0" is automatically added by snprintf
  snprintf(dest + write_idx, len - write_idx, suffix);
  write_idx += sizeof(suffix) - 1;
  return write_idx;
}

static std::string MakeFileName(const std::string& name, uint64_t number,
                                const char* suffix) {
  char buf[100];
  snprintf(buf, sizeof(buf), "/%06" PRIu64 ".%s", number, suffix);
  return name + buf;
}

std::string LogFileName(const std::string& name, uint64_t number) {
  assert(number > 0);
  return MakeFileName(name, number, "log");
}

std::string ArchivalDirectory(const std::string& dir) {
  return dir + "/" + ARCHIVAL_DIR;
}
std::string ArchivedLogFileName(const std::string& name, uint64_t number) {
  assert(number > 0);
  return MakeFileName(name + "/" + ARCHIVAL_DIR, number, "log");
}

std::string MakeTableFileName(const std::string& path, uint64_t number) {
  return MakeFileName(path, number, kRocksDbTFileExt);
}

std::string MakeTableDataFilePath(const std::string& path, uint64_t number) {
  return MakeFileName(path, number, kRocksDbTSBlockFileExt);
}

std::string Rocks2LevelTableFileName(const std::string& fullname) {
  assert(fullname.size() > sizeof(kRocksDbTFileExt));
  if (fullname.size() <= sizeof(kRocksDbTFileExt)) {
    return "";
  }
  return fullname.substr(0, fullname.size() - sizeof(kRocksDbTFileExt) + 1) +
         kLevelDbTFileExt;
}

uint64_t TableFileNameToNumber(const std::string& name) {
  uint64_t number = 0;
  uint64_t base = 1;
  int pos = static_cast<int>(name.find_last_of('.'));
  while (--pos >= 0 && name[pos] >= '0' && name[pos] <= '9') {
    number += (name[pos] - '0') * base;
    base *= 10;
  }
  return number;
}

std::string TableFileName(const std::vector<DbPath>& db_paths, uint64_t number,
                          uint32_t path_id) {
  assert(number > 0);
  std::string path;
  if (path_id >= db_paths.size()) {
    path = db_paths.back().path;
  } else {
    path = db_paths[path_id].path;
  }
  return MakeTableFileName(path, number);
}

extern std::string TableBaseToDataFileName(const std::string& base_fname) {
  return base_fname + "." + kRocksDbTSBlockExtSuffix + ".0";
}

void FormatFileNumber(uint64_t number, uint32_t path_id, char* out_buf,
                      size_t out_buf_size) {
  if (path_id == 0) {
    snprintf(out_buf, out_buf_size, "%" PRIu64, number);
  } else {
    snprintf(out_buf, out_buf_size, "%" PRIu64
                                    "(path "
                                    "%" PRIu32 ")",
             number, path_id);
  }
}

std::string DescriptorFileName(const std::string& dbname, uint64_t number) {
  assert(number > 0);
  char buf[100];
  snprintf(buf, sizeof(buf), "/MANIFEST-%06" PRIu64, number);
  return dbname + buf;
}

std::string CurrentFileName(const std::string& dbname) {
  return dbname + "/CURRENT";
}

std::string LockFileName(const std::string& dbname) {
  return dbname + "/LOCK";
}

std::string TempFileName(const std::string& dbname, uint64_t number) {
  return MakeFileName(dbname, number, kTempFileNameSuffix);
}

InfoLogPrefix::InfoLogPrefix(bool has_log_dir,
                             const std::string& db_absolute_path) {
  if (!has_log_dir) {
    const char kInfoLogPrefix[] = "LOG";
    // "\0" is automatically added to the end
    snprintf(buf, sizeof(buf), kInfoLogPrefix);
    prefix = Slice(buf, sizeof(kInfoLogPrefix) - 1);
  } else {
    size_t len = GetInfoLogPrefix(db_absolute_path, buf, sizeof(buf));
    prefix = Slice(buf, len);
  }
}

std::string InfoLogFileName(const std::string& dbname,
    const std::string& db_path, const std::string& log_dir) {
  if (log_dir.empty()) {
    return dbname + "/LOG";
  }

  InfoLogPrefix info_log_prefix(true, db_path);
  return log_dir + "/" + info_log_prefix.buf;
}

// Return the name of the old info log file for "dbname".
std::string OldInfoLogFileName(const std::string& dbname, uint64_t ts,
    const std::string& db_path, const std::string& log_dir) {
  char buf[50];
  snprintf(buf, sizeof(buf), "%" PRIu64, ts);

  if (log_dir.empty()) {
    return dbname + "/LOG.old." + buf;
  }

  InfoLogPrefix info_log_prefix(true, db_path);
  return log_dir + "/" + info_log_prefix.buf + ".old." + buf;
}

std::string OptionsFileName(const std::string& dbname, uint64_t file_num) {
  char buffer[256];
  snprintf(buffer, sizeof(buffer), "%s%06" PRIu64,
           kOptionsFileNamePrefix, file_num);
  return dbname + "/" + buffer;
}

std::string TempOptionsFileName(const std::string& dbname, uint64_t file_num) {
  char buffer[256];
  snprintf(buffer, sizeof(buffer), "%s%06" PRIu64 ".%s",
           kOptionsFileNamePrefix, file_num,
           kTempFileNameSuffix);
  return dbname + "/" + buffer;
}

std::string MetaDatabaseName(const std::string& dbname, uint64_t number) {
  char buf[100];
  snprintf(buf, sizeof(buf), "/METADB-%" PRIu64, number);
  return dbname + buf;
}

std::string IdentityFileName(const std::string& dbname) {
  return dbname + "/IDENTITY";
}

// Owned filenames have the form:
//    dbname/IDENTITY
//    dbname/CURRENT
//    dbname/LOCK
//    dbname/<info_log_name_prefix>
//    dbname/<info_log_name_prefix>.old.[0-9]+
//    dbname/MANIFEST-[0-9]+
//    dbname/[0-9]+.(log|sst)
//    dbname/METADB-[0-9]+
//    dbname/OPTIONS-[0-9]+
//    dbname/OPTIONS-[0-9]+.dbtmp
//    Disregards / at the beginning
bool ParseFileName(const std::string& fname,
                   uint64_t* number,
                   FileType* type,
                   WalFileType* log_type) {
  return ParseFileName(fname, number, "", type, log_type);
}

bool ParseFileName(const std::string& fname, uint64_t* number,
                   const Slice& info_log_name_prefix, FileType* type,
                   WalFileType* log_type) {
  Slice rest(fname);
  if (fname.length() > 1 && fname[0] == '/') {
    rest.remove_prefix(1);
  }
  if (rest == "IDENTITY") {
    *number = 0;
    *type = kIdentityFile;
  } else if (rest == "CURRENT") {
    *number = 0;
    *type = kCurrentFile;
  } else if (rest == "LOCK") {
    *number = 0;
    *type = kDBLockFile;
  } else if (info_log_name_prefix.size() > 0 &&
             rest.starts_with(info_log_name_prefix)) {
    rest.remove_prefix(info_log_name_prefix.size());
    if (rest == "" || rest == ".old") {
      *number = 0;
      *type = kInfoLogFile;
    } else if (rest.starts_with(".old.")) {
      uint64_t ts_suffix;
      // sizeof also counts the trailing '\0'.
      rest.remove_prefix(sizeof(".old.") - 1);
      if (!ConsumeDecimalNumber(&rest, &ts_suffix)) {
        return false;
      }
      *number = ts_suffix;
      *type = kInfoLogFile;
    }
  } else if (rest.starts_with("MANIFEST-")) {
    rest.remove_prefix(strlen("MANIFEST-"));
    uint64_t num;
    if (!ConsumeDecimalNumber(&rest, &num)) {
      return false;
    }
    if (!rest.empty()) {
      return false;
    }
    *type = kDescriptorFile;
    *number = num;
  } else if (rest.starts_with("METADB-")) {
    rest.remove_prefix(strlen("METADB-"));
    uint64_t num;
    if (!ConsumeDecimalNumber(&rest, &num)) {
      return false;
    }
    if (!rest.empty()) {
      return false;
    }
    *type = kMetaDatabase;
    *number = num;
  } else if (rest.starts_with(kOptionsFileNamePrefix)) {
    uint64_t ts_suffix;
    bool is_temp_file = false;
    rest.remove_prefix(sizeof(kOptionsFileNamePrefix) - 1);
    const std::string kTempFileNameSuffixWithDot =
        std::string(".") + kTempFileNameSuffix;
    if (rest.ends_with(kTempFileNameSuffixWithDot)) {
      rest.remove_suffix(kTempFileNameSuffixWithDot.size());
      is_temp_file = true;
    }
    if (!ConsumeDecimalNumber(&rest, &ts_suffix)) {
      return false;
    }
    *number = ts_suffix;
    *type = is_temp_file ? kTempFile : kOptionsFile;
  } else {
    // Avoid strtoull() to keep filename format independent of the
    // current locale
    bool archive_dir_found = false;
    if (rest.starts_with(ARCHIVAL_DIR)) {
      if (rest.size() < sizeof(ARCHIVAL_DIR)) {
        return false;
      }
      rest.remove_prefix(sizeof(ARCHIVAL_DIR)); // Don't subtract 1 to remove / also
      if (log_type) {
        *log_type = kArchivedLogFile;
      }
      archive_dir_found = true;
    }
    uint64_t num;
    if (!ConsumeDecimalNumber(&rest, &num)) {
      return false;
    }
    if (rest.size() <= 1 || rest[0] != '.') {
      return false;
    }
    rest.remove_prefix(1);

    Slice suffix = rest;
    if (suffix == Slice("log")) {
      *type = kLogFile;
      if (log_type && !archive_dir_found) {
        *log_type = kAliveLogFile;
      }
    } else if (archive_dir_found) {
      return false; // Archive dir can contain only log files
    } else if (suffix == Slice(kRocksDbTFileExt) ||
               suffix == Slice(kLevelDbTFileExt)) {
      *type = kTableFile;
    } else if (suffix == Slice(kRocksDbTSBlockFileExt)) {
      *type = kTableSBlockFile;
    } else if (suffix == Slice(kTempFileNameSuffix)) {
      *type = kTempFile;
    } else {
      return false;
    }
    *number = num;
  }
  return true;
}

Status SetCurrentFile(Env* env, const std::string& dbname,
                      uint64_t descriptor_number,
                      Directory* directory_to_fsync,
                      bool disable_data_sync) {
  // Remove leading "dbname/" and add newline to manifest file name
  std::string manifest = DescriptorFileName(dbname, descriptor_number);
  Slice contents = manifest;
  assert(contents.starts_with(dbname + "/"));
  contents.remove_prefix(dbname.size() + 1);
  std::string tmp = TempFileName(dbname, descriptor_number);
  Status s = WriteStringToFile(env, contents.ToString() + "\n", tmp, !disable_data_sync);
  if (s.ok()) {
    TEST_KILL_RANDOM("SetCurrentFile:0", test_kill_odds * REDUCE_ODDS2);
    s = env->RenameFile(tmp, CurrentFileName(dbname));
    TEST_KILL_RANDOM("SetCurrentFile:1", test_kill_odds * REDUCE_ODDS2);
  }
  if (s.ok()) {
    if (directory_to_fsync != nullptr && !disable_data_sync) {
      RETURN_NOT_OK(directory_to_fsync->Fsync());
    }
  } else {
    env->CleanupFile(tmp);
  }
  return s;
}

Status SetIdentityFile(Env* env, const std::string& dbname) {
  std::string id = env->GenerateUniqueId();
  assert(!id.empty());
  // Reserve the filename dbname/000000.dbtmp for the temporary identity file
  std::string tmp = TempFileName(dbname, 0);
  Status s = WriteStringToFile(env, id, tmp, true);
  if (s.ok()) {
    s = env->RenameFile(tmp, IdentityFileName(dbname));
  }
  if (!s.ok()) {
    env->CleanupFile(tmp);
  }
  return s;
}

Status SyncManifest(Env* env, const DBOptions* db_options,
                    WritableFileWriter* file) {
  TEST_KILL_RANDOM("SyncManifest:0", test_kill_odds * REDUCE_ODDS2);
  if (db_options->disableDataSync) {
    return Status::OK();
  } else {
    return file->Sync(db_options->use_fsync);
  }
}

}  // namespace rocksdb
