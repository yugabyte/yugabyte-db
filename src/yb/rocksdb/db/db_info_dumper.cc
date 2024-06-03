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

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include "yb/rocksdb/db/db_info_dumper.h"

#include <inttypes.h>
#include <stdio.h>
#include <string>
#include <algorithm>
#include <vector>

#include "yb/rocksdb/db/filename.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/env.h"

namespace rocksdb {

void HandleLogFile(const DBOptions& options, const std::string& path, const std::string& file,
                   std::string* wal_info) {
  uint64_t file_size;
  auto status = options.env->GetFileSize(path + "/" + file, &file_size);
  if (status.ok()) {
    char str[16];
    snprintf(str, sizeof(str), "%" PRIu64, file_size);
    wal_info->append(file).append(" size: ").append(str).append(" ; ");
  } else {
    RHEADER(options.info_log, "Failed to get log file size %s: %s\n",
            file.c_str(), status.ToString().c_str());
  }
}

void DumpDBFileSummary(const DBOptions& options, const std::string& dbname) {
  if (options.info_log == nullptr) {
    return;
  }

  auto* env = options.env;
  uint64_t number = 0;
  FileType type = kInfoLogFile;

  std::vector<std::string> files;
  uint64_t file_num = 0;
  uint64_t file_size;
  std::string file_info, wal_info;

  RHEADER(options.info_log, "DB SUMMARY\n");
  // Get files in dbname dir
  if (!env->GetChildren(dbname, &files).ok()) {
    RERROR(options.info_log,
        "Error when reading %s dir\n", dbname.c_str());
  }
  std::sort(files.begin(), files.end());
  for (std::string file : files) {
    if (!ParseFileName(file, &number, &type)) {
      continue;
    }
    switch (type) {
      case kCurrentFile:
        RHEADER(options.info_log, "CURRENT file:  %s\n", file.c_str());
        break;
      case kIdentityFile:
        RHEADER(options.info_log, "IDENTITY file:  %s\n", file.c_str());
        break;
      case kDescriptorFile: {
          auto status = env->GetFileSize(dbname + "/" + file, &file_size);
          if (status.ok()) {
            RHEADER(options.info_log, "MANIFEST file:  %s size: %" PRIu64 " Bytes\n",
                file.c_str(), file_size);
          } else {
            RHEADER(options.info_log, "Failed to get MANIFEST file size %s: %s\n",
                    file.c_str(), status.ToString().c_str());
          }
        } break;
      case kLogFile:
        HandleLogFile(options, dbname, file, &wal_info);
        break;
      case kTableFile:
        if (++file_num < 10) {
          file_info.append(file).append(" ");
        }
        break;
      default:
        break;
    }
  }

  // Get sst files in db_path dir
  for (auto& db_path : options.db_paths) {
    if (dbname.compare(db_path.path) != 0) {
      if (!env->GetChildren(db_path.path, &files).ok()) {
        RERROR(options.info_log,
            "Error when reading %s dir\n",
            db_path.path.c_str());
        continue;
      }
      std::sort(files.begin(), files.end());
      for (std::string file : files) {
        if (ParseFileName(file, &number, &type)) {
          if (type == kTableFile && ++file_num < 10) {
            file_info.append(file).append(" ");
          }
        }
      }
    }
    RHEADER(options.info_log,
        "SST files in %s dir, Total Num: %" PRIu64 ", files: %s\n",
        db_path.path.c_str(), file_num, file_info.c_str());
    file_num = 0;
    file_info.clear();
  }

  // Get wal file in wal_dir
  if (dbname.compare(options.wal_dir) != 0) {
    if (!env->GetChildren(options.wal_dir, &files).ok()) {
      RERROR(options.info_log,
          "Error when reading %s dir\n",
          options.wal_dir.c_str());
      return;
    }
    wal_info.clear();
    for (std::string file : files) {
      if (ParseFileName(file, &number, &type)) {
        if (type == kLogFile) {
          HandleLogFile(options, options.wal_dir, file, &wal_info);
        }
      }
    }
  }
  RHEADER(options.info_log, "Write Ahead Log file in %s: %s\n",
      options.wal_dir.c_str(), wal_info.c_str());
}
}  // namespace rocksdb
