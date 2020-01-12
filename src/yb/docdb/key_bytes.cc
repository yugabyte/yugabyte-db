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

#include "yb/docdb/key_bytes.h"

#include "yb/common/doc_hybrid_time.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rocksutil/yb_rocksdb.h"

using strings::Substitute;
using std::string;

namespace yb {
namespace docdb {

void AppendDocHybridTime(const DocHybridTime& doc_ht, KeyBytes* key) {
  key->AppendValueType(ValueType::kHybridTime);
  doc_ht.AppendEncodedInDocDbFormat(key->mutable_data());
}

}  // namespace docdb
}  // namespace yb
