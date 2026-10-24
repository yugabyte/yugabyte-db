// Copyright (c) YugabyteDB, Inc.
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

#include "yb/util/protobuf_util.h"

#include "yb/util/coding-inl.h"
#include "yb/util/coding.h"
#include "yb/util/crc.h"
#include "yb/util/env.h"
#include "yb/util/env_util.h"
#include "yb/util/faststring.h"
#include "yb/util/path_util.h"
#include "yb/util/pb_util.h"
#include "yb/util/status_format.h"

namespace yb {

namespace {

constexpr size_t kChecksumSize = sizeof(uint32_t);

} // namespace

Status WritePBWithCRCToPath(
    Env* env, const std::string& path, const google::protobuf::MessageLite& msg) {
  faststring data;
  data.resize(kChecksumSize);
  RETURN_NOT_OK(pb_util::AppendToString(msg, &data));
  InlineEncodeFixed32(
      data.data(), crc::Crc32c(data.data() + kChecksumSize, data.size() - kChecksumSize));

  std::string tmp_path;
  std::unique_ptr<WritableFile> file;
  RETURN_NOT_OK(env->NewTempWritableFile(
      WritableFileOptions(), pb_util::MakeTempPath(path), &tmp_path, &file));
  env_util::ScopedFileDeleter tmp_deleter(env, tmp_path);
  RETURN_NOT_OK(file->Append(data));
  RETURN_NOT_OK(file->Sync());
  RETURN_NOT_OK(file->Close());
  RETURN_NOT_OK_PREPEND(env->RenameFile(tmp_path, path), "Failed to rename tmp file to " + path);
  tmp_deleter.Cancel();
  return env->SyncDir(DirName(path));
}

Status ReadPBWithCRCFromPath(
    Env* env, const std::string& path, google::protobuf::MessageLite* msg) {
  faststring data;
  RETURN_NOT_OK(ReadFileToString(env, path, &data));
  SCHECK_GE(data.size(), kChecksumSize, Corruption, Format("Truncated file $0", path));
  const Slice body(data.data() + kChecksumSize, data.size() - kChecksumSize);
  SCHECK_EQ(
      crc::Crc32c(body), DecodeFixed32(data.data()), Corruption,
      Format("Checksum mismatch in $0", path));
  return pb_util::ParseFromArray(msg, body.data(), body.size());
}

} // namespace yb
