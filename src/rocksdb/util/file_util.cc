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
#include "util/file_util.h"

#include <string>
#include <algorithm>

#include "rocksdb/env.h"
#include "rocksdb/options.h"
#include "util/sst_file_manager_impl.h"
#include "util/file_reader_writer.h"

namespace rocksdb {

// Utility function to copy a file up to a specified length
Status CopyFile(Env* env, const std::string& source,
                const std::string& destination, uint64_t size) {
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

  char buffer[4096];
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

Status DeleteSSTFile(const DBOptions* db_options, const std::string& fname,
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

}  // namespace rocksdb
