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
#include "yb/rocksdb/util/file_util.h"

#include <vector>
#include <algorithm>

#include "yb/rocksdb/env.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/util/sst_file_manager_impl.h"
#include "yb/rocksdb/util/file_reader_writer.h"
#include "yb/gutil/strings/substitute.h"

namespace rocksdb {

using std::string;
using strings::Substitute;

// Utility function to copy a file up to a specified length
Status CopyFile(Env* env, const string& source,
                const string& destination, uint64_t size) {
  const EnvOptions soptions;
  Status s;
  unique_ptr<SequentialFileReader> src_reader;
  unique_ptr<WritableFileWriter> dest_writer;

  {
    unique_ptr<SequentialFile> srcfile;
    s = env->NewSequentialFile(source, &srcfile, soptions);
    unique_ptr<WritableFile> destfile;
    if (s.ok()) {
      s = env->NewWritableFile(destination, &destfile, soptions);
    } else {
      return s;
    }

    if (size == 0) {
      // default argument means copy everything
      if (s.ok()) {
        s = env->GetFileSize(source, &size);
      } else {
        return s;
      }
    }
    src_reader.reset(new SequentialFileReader(std::move(srcfile)));
    dest_writer.reset(new WritableFileWriter(std::move(destfile), soptions));
  }

  uint8_t buffer[4096];
  Slice slice;
  while (size > 0) {
    size_t bytes_to_read = std::min(sizeof(buffer), static_cast<size_t>(size));
    if (s.ok()) {
      s = src_reader->Read(bytes_to_read, &slice, buffer);
    }
    if (s.ok()) {
      if (slice.size() == 0) {
        return STATUS(Corruption, "file too small");
      }
      s = dest_writer->Append(slice);
    }
    if (!s.ok()) {
      return s;
    }
    size -= slice.size();
  }
  return Status::OK();
}

Status DeleteSSTFile(const DBOptions* db_options, const string& fname,
                     uint32_t path_id) {
  // TODO(tec): support sst_file_manager for multiple path_ids
  auto sfm =
      static_cast<SstFileManagerImpl*>(db_options->sst_file_manager.get());
  if (sfm && path_id == 0) {
    return sfm->ScheduleFileDeletion(fname);
  } else {
    return db_options->env->DeleteFile(fname);
  }
}

Status CopyDirectory(Env* env, const string& src_dir, const string& dest_dir,
                     CreateIfMissing create_if_missing, UseHardLinks use_hard_links) {
  RETURN_NOT_OK_PREPEND(
      env->FileExists(src_dir),
      Substitute("Source directory does not exist: $0", src_dir));

  Status s = env->FileExists(dest_dir);
  if (!s.ok()) {
    if (create_if_missing) {
      RETURN_NOT_OK_PREPEND(
          env->CreateDir(dest_dir),
          Substitute("Cannot create destination directory: $0", dest_dir));
    } else {
      return s.CloneAndPrepend(Substitute("Destination directory does not exist: $0", dest_dir));
    }
  }

  // Copy files.
  std::vector<string> files;
  RETURN_NOT_OK_PREPEND(
      env->GetChildren(src_dir, &files),
      Substitute("Cannot get list of files for directory: $0", src_dir));

  for (const string& file : files) {
    if (file != "." && file != "..") {
      const string file_path = src_dir + '/' + file;
      const string target_path = dest_dir + '/' + file;

      if (use_hard_links) {
        s = env->LinkFile(file_path, target_path);

        if (s.ok()) {
          continue;
        }
      }

      if (env->DirExists(file_path)) {
        RETURN_NOT_OK_PREPEND(
            CopyDirectory(env, file_path, target_path, CreateIfMissing::kTrue, use_hard_links),
            yb::Format("Cannot copy directory: $0", file_path));
      } else {
        // Last argument size == 0 means coping whole file.
        RETURN_NOT_OK_PREPEND(
            CopyFile(env, file_path, target_path, 0),
            yb::Format("Cannot copy file: $0", file_path));
      }
    }
  }

  return Status::OK();
}

}  // namespace rocksdb
