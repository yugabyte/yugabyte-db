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
#ifndef ROCKSDB_LITE

#include "yb/docdb/docdb_debug.h"
#include "yb/docdb/schema_packing.h"

#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/sst_dump_tool.h"

namespace yb {
namespace {

class DocDBKVFormatterImpl : public rocksdb::DocDBKVFormatter {
  std::string Format(
      const Slice& key, const Slice& value, docdb::StorageDbType type) const override {
    return docdb::EntryToString(rocksdb::ExtractUserKey(key), value, schema_packing_storage_, type);
  }

  docdb::SchemaPackingStorage schema_packing_storage_;
};

}  // namespace
} // namespace yb

int main(int argc, char** argv) {
  yb::DocDBKVFormatterImpl docdb_kv_formatter;
  rocksdb::SSTDumpTool tool(&docdb_kv_formatter);
  tool.Run(argc, argv);
  return 0;
}
#else
#include <stdio.h>
int main(int argc, char** argv) {
  fprintf(stderr, "Not supported in lite mode.\n");
  return 1;
}
#endif  // ROCKSDB_LITE
