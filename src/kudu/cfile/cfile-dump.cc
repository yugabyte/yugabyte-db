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

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <iostream>

#include "kudu/cfile/cfile_reader.h"
#include "kudu/cfile/cfile_util.h"
#include "kudu/fs/block_id.h"
#include "kudu/fs/fs_manager.h"
#include "kudu/util/logging.h"
#include "kudu/util/flags.h"

DEFINE_bool(print_meta, true, "print the header and footer from the file");
DEFINE_bool(iterate_rows, true, "iterate each row in the file");
DEFINE_bool(print_rows, true, "print each row in the file");
DEFINE_int32(num_iterations, 1, "number of times to iterate the file");

namespace kudu {
namespace cfile {

using std::string;
using std::cout;
using std::endl;

Status DumpFile(const string& block_id_str) {
  // Allow read-only access to live blocks.
  FsManagerOpts fs_opts;
  fs_opts.read_only = true;
  FsManager fs_manager(Env::Default(), fs_opts);
  RETURN_NOT_OK(fs_manager.Open());

  uint64_t numeric_id;
  CHECK(safe_strtou64_base(block_id_str, &numeric_id, 16));
  BlockId block_id(numeric_id);
  gscoped_ptr<fs::ReadableBlock> block;
  RETURN_NOT_OK(fs_manager.OpenBlock(block_id, &block));

  gscoped_ptr<CFileReader> reader;
  RETURN_NOT_OK(CFileReader::Open(block.Pass(), ReaderOptions(), &reader));

  if (FLAGS_print_meta) {
    cout << "Header:\n" << reader->header().DebugString() << endl;
    cout << "Footer:\n" << reader->footer().DebugString() << endl;
  }

  if (FLAGS_iterate_rows) {
    gscoped_ptr<CFileIterator> it;
    RETURN_NOT_OK(reader->NewIterator(&it, CFileReader::DONT_CACHE_BLOCK));

    DumpIteratorOptions opts;
    opts.print_rows = FLAGS_print_rows;
    for (int i = 0; i < FLAGS_num_iterations; i++) {
      RETURN_NOT_OK(it->SeekToFirst());
      RETURN_NOT_OK(DumpIterator(*reader, it.get(), &cout, opts, 0));
    }
  }

  return Status::OK();
}

} // namespace cfile
} // namespace kudu

int main(int argc, char **argv) {
  kudu::ParseCommandLineFlags(&argc, &argv, true);
  kudu::InitGoogleLoggingSafe(argv[0]);
  if (argc != 2) {
    std::cerr << "usage: " << argv[0]
              << " -fs_wal_dir <dir> -fs_data_dirs <dirs> <block id>" << std::endl;
    return 1;
  }

  if (!FLAGS_iterate_rows) {
    FLAGS_print_rows = false;
  }

  kudu::Status s = kudu::cfile::DumpFile(argv[1]);
  if (!s.ok()) {
    std::cerr << "Error: " << s.ToString() << std::endl;
    return 1;
  }

  return 0;
}
