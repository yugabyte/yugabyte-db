// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
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

#include <iostream>
#include <string>

#include "yb/encryption/encrypted_file_factory.h"
#include "yb/encryption/header_manager_impl.h"
#include "yb/encryption/universe_key_manager.h"
#include "yb/util/env.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/pb_util.h"
#include "yb/util/status.h"

using yb::encryption::UniverseKeyManager;
using yb::Status;
using std::cerr;
using std::endl;
using std::string;

DEFINE_NON_RUNTIME_bool(oneline, false, "print each protobuf on a single line");
TAG_FLAG(oneline, stable);

namespace yb {
namespace pb_util {

Status DumpPBContainerFile(const string& filename, const string& keyfile,
                           const string& keyid) {

  std::unique_ptr<RandomAccessFile> reader;

  // If keyfile or keyid are not provided, assume file is unencrypted.
  if (keyfile.empty() || keyid.empty()) {
    RETURN_NOT_OK(Env::Default()->NewRandomAccessFile(filename, &reader));
  } else {
    std::unique_ptr<Env> env;
    std::unique_ptr<UniverseKeyManager> universe_key_manager;
    faststring key_file_content;
    Status s = ReadFileToString(Env::Default(), keyfile, &key_file_content);
    std::string keydata = key_file_content.ToString();

    if(!s.ok()) {
      LOG(FATAL) << yb::Format("Could not read key file at path $0: $1", keyfile, s.ToString());
    }

    auto res = UniverseKeyManager::FromKey(keyid, yb::Slice(keydata));
    if (!res.ok()) {
      LOG(FATAL) << "Could not create universe key manager: " << res.status().ToString();
    }

    universe_key_manager = std::move(*res);
    env = yb::encryption::NewEncryptedEnv(
        yb::encryption::DefaultHeaderManager(universe_key_manager.get()));
    RETURN_NOT_OK(env->NewRandomAccessFile(filename, &reader));
  }

  ReadablePBContainerFile pb_reader(std::move(reader));
  RETURN_NOT_OK(pb_reader.Init());
  RETURN_NOT_OK(pb_reader.Dump(&std::cout, FLAGS_oneline));

  return Status::OK();
}

} // namespace pb_util
} // namespace yb

int main(int argc, char **argv) {
  yb::ParseCommandLineFlags(&argc, &argv, true);
  yb::InitGoogleLoggingSafe(argv[0]);
  if (argc != 2 && argc != 4) {
    cerr << "usage: " << argv[0] << "<protobuf container filename> [<keyfile> <keyid>]" << endl;
    cerr << "For encrypted files, provide the universe key file and the id of universe key" << endl;
    return 2;
  }

  std::string protobuf_file = argv[1];
  // The keyfile and keyid are optional. If not provided, the default key will be used.
  // The keyfile is the path to the file containing the universe key.
  // The keyid is the ID of the universe key.
  std::string keyfile = (argc > 2) ? argv[2] : "";
  std::string keyid = (argc > 3) ? argv[3] : "";

  Status s = yb::pb_util::DumpPBContainerFile(protobuf_file, keyfile, keyid);
  if (s.ok()) {
    return 0;
  } else {
    cerr << s.ToString() << endl;
    return 1;
  }
}
