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

// Decodes intent RocksDB key. intent_prefix should point to slice to hold intent prefix
// (kIntentPrefix + SubDocKey (no HT).
// intent_type and doc_ht are optional parameters (could be nullptr) to store decoded
// intent type and intent doc hybrid time.
CHECKED_STATUS DecodeIntentKey(const Slice &encoded_intent_key, Slice* intent_prefix,
                               IntentType* intent_type, DocHybridTime* doc_ht);

// Decodes transaction ID from intent value. Consumes it from intent_value slice.
Result<TransactionId> DecodeTransactionIdFromIntentValue(Slice* intent_value);

enum class IntentKind {
  // "Weak" intents are written for ancecstor keys of a key that's being modified. For example, if
  // we're writing a.b.c with snapshot isolation, we'll write weak snapshot isolation intents for
  // keys "a" and "a.b".
  kWeak,

  // "Strong" intents are written for keys that are being modified. In the example above, we will
  // write a strong snapshot isolation intent for the key a.b.c itself.
  kStrong
};

struct IntentTypePair {
  docdb::IntentType strong;
  docdb::IntentType weak;

  docdb::IntentType operator[](IntentKind kind) {
    return kind == IntentKind::kWeak ? weak : strong;
  }
};

IntentTypePair GetWriteIntentsForIsolationLevel(IsolationLevel level);

inline void AppendIntentKeySuffix(
    docdb::IntentType intent_type, const DocHybridTime& doc_ht, KeyBytes* key) {
  AppendIntentType(intent_type, key);
  AppendDocHybridTime(doc_ht, key);
}

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
