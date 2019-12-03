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

// DecodeIntentKey result.
// intent_prefix - intent prefix (SubDocKey (no HT)).
struct DecodedIntentKey {
  Slice intent_prefix;
  IntentTypeSet intent_types;
  DocHybridTime doc_ht;

  std::string ToString() const {
    return Format("{ intent_prefix: $0 intent_types: $1 doc_ht: $2 }",
                  intent_prefix.ToDebugHexString(), intent_types, doc_ht);
  }
};

inline std::ostream& operator<<(std::ostream& out, const DecodedIntentKey& decoded_intent_key) {
  return out << decoded_intent_key.ToString();
}

// Decodes intent RocksDB key.
Result<DecodedIntentKey> DecodeIntentKey(const Slice &encoded_intent_key);

// Decode intent RocksDB value.
// encoded_intent_value - input intent value to decode.
// transaction_id_slice - input transaction id (to double-check with transaction id in value).
// write_id - output write id.
// body - output the rest of the data after write id.
CHECKED_STATUS DecodeIntentValue(
    const Slice& encoded_intent_value, const Slice& transaction_id_slice, IntraTxnWriteId* write_id,
    Slice* body);

// Decodes transaction ID from intent value. Consumes it from intent_value slice.
Result<TransactionId> DecodeTransactionIdFromIntentValue(Slice* intent_value);

// "Weak" intents are written for ancestor keys of a key that's being modified. For example, if
// we're writing a.b.c with snapshot isolation, we'll write weak snapshot isolation intents for
// keys "a" and "a.b".
//
// "Strong" intents are written for keys that are being modified. In the example above, we will
// write a strong snapshot isolation intent for the key a.b.c itself.
YB_DEFINE_ENUM(IntentStrength, (kWeak)(kStrong));

YB_DEFINE_ENUM(OperationKind, (kRead)(kWrite));

IntentTypeSet GetStrongIntentTypeSet(
    IsolationLevel level, OperationKind operation_kind, RowMarkType row_mark);

inline IntentTypeSet StrongToWeak(IntentTypeSet inp) {
  IntentTypeSet result(inp.ToUIntPtr() >> kStrongIntentFlag);
  DCHECK((inp & result).None());
  return result;
}

inline IntentTypeSet WeakToStrong(IntentTypeSet inp) {
  IntentTypeSet result(inp.ToUIntPtr() << kStrongIntentFlag);
  DCHECK((inp & result).None());
  return result;
}

bool HasStrong(IntentTypeSet inp);

IntentTypeSet ObsoleteIntentTypeToSet(uint8_t obsolete_intent_type);
IntentTypeSet ObsoleteIntentTypeSetToNew(uint8_t obsolete_intent_type_set);

// Returns true if ch is value type of one of intent types, obsolete or not.
bool IntentValueType(char ch);

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_INTENT_H_
