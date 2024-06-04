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

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider fdp(data, size);

  auto dockey = GetDocKey(&fdp, /* consume_all */ false);
  dockv::SubDocKey subdockey(dockey);

  constexpr int kMaxNumSubKeys = 30;
  for (size_t i = 0; i < fdp.ConsumeIntegralInRange<uint8_t>(1, kMaxNumSubKeys);
       i++) {
    subdockey.AppendSubKeysAndMaybeHybridTime(GetKeyEntryValue(&fdp));
  }
  // (kMaxHybridTimeValue - 1) is reserved for kInvalidHybridTimeValue,
  // hence upper bound here is (kMaxHybridTimeValue - 2)
  uint64_t hybrid_time = fdp.ConsumeIntegralInRange<uint64_t>(
      kMinHybridTimeValue, kMaxHybridTimeValue - 2);
  subdockey.set_hybrid_time(DocHybridTime(HybridTime(hybrid_time)));

  auto encoded_key = subdockey.Encode();
  dockv::SubDocKey decoded_key;
  CHECK_OK(decoded_key.FullyDecodeFrom(encoded_key.AsSlice()));
  CHECK_EQ(subdockey, decoded_key);
  auto reencoded_doc_key = decoded_key.Encode();
  CHECK_EQ(encoded_key, reencoded_doc_key);
  return 0;
}

}  // namespace fuzz
}  // namespace docdb
}  // namespace yb
