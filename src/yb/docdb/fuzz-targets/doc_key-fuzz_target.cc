// Copyright (c) YugaByte, Inc.
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

#include "yb/docdb/fuzz-targets/docdb_fuzz_target_util.h"

namespace yb {
namespace docdb {
namespace fuzz {

// Entry point for LibFuzzer
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider fdp(data, size);

  auto dockey = GetDocKey(&fdp, /* consume_all */ true);

  auto encoded_key = dockey.Encode();
  dockv::DocKey decoded_key;
  CHECK_OK(decoded_key.FullyDecodeFrom(encoded_key.AsSlice()));
  CHECK_EQ(dockey, decoded_key);
  auto reencoded_doc_key = decoded_key.Encode();
  CHECK_EQ(encoded_key, reencoded_doc_key);
  return 0;
}

}  // namespace fuzz
}  // namespace docdb
}  // namespace yb
