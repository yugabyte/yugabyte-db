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

#include "yb/util/env.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/pb_util.h"
#include "yb/util/status.h"

using yb::Status;
using std::cerr;
using std::endl;
using std::string;

DEFINE_UNKNOWN_bool(oneline, false, "print each protobuf on a single line");
TAG_FLAG(oneline, stable);

namespace yb {
namespace pb_util {

Status DumpPBContainerFile(const string& filename) {
  Env* env = Env::Default();
  std::unique_ptr<RandomAccessFile> reader;
  RETURN_NOT_OK(env->NewRandomAccessFile(filename, &reader));
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
  if (argc != 2) {
    cerr << "usage: " << argv[0] << " [--oneline] <protobuf container filename>" << endl;
    return 2;
  }

  Status s = yb::pb_util::DumpPBContainerFile(argv[1]);
  if (s.ok()) {
    return 0;
  } else {
    cerr << s.ToString() << endl;
    return 1;
  }
}
