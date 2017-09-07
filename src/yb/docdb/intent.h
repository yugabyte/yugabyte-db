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

#ifndef YB_DOCDB_INTENT_H_
#define YB_DOCDB_INTENT_H_

#include "yb/docdb/value.h"
#include "yb/docdb/doc_key.h"

namespace yb {
namespace docdb {

// The intent class is a wrapper around transaction id, (optional value) and
// type of intent (which includes isolation level)
class Intent {
 public:
  Intent() {}
  Intent(
      SubDocKey subdoc_key, IntentType intent_type, const Uuid& transaction_id, Value&& value)
      : subdoc_key_(subdoc_key),
        intent_type_(intent_type),
        transaction_id_(transaction_id),
        value_(std::move(value)) {}

  std::string ToString() const;

  // Encode the intent into the rocksdb-key and rocksdb-value:
  // respectively (SubDocKey, IntentType) in key, and (TransactionId, Value) in value.
  // The EncodeKey() function changes the internal state only temporarily.
  std::string EncodeKey();
  std::string EncodeValue() const;

  // DecodeFromKey only partially decodes the Intent, the fields (SubDocKey, IntentType).
  CHECKED_STATUS DecodeFromKey(const Slice &encoded_intent);
  // DecodeFromValue only partially decodes the Intent, the fields (TransactionId, Value).
  CHECKED_STATUS DecodeFromValue(const Slice &encoded_intent);
 private:

  SubDocKey subdoc_key_;
  IntentType intent_type_;
  Uuid transaction_id_;
  Value value_;
};

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_INTENT_H_
