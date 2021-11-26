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

#include "yb/common/transaction.h"
#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/value.h"

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

struct DecodedIntentValue {
  // Decoded transaction_id. Nil() value can mean that the transaction_id was not decoded, but not
  // necessarily that it was not present.
  TransactionId transaction_id = TransactionId::Nil();
  // Subtransaction id or defaults to kMinSubtransactionId.
  SubTransactionId subtransaction_id;
  // Decoded write id.
  IntraTxnWriteId write_id;
  // The rest of the data after write id.
  Slice body;
};

// Decode intent RocksDB value.
// encoded_intent_value - input intent value to decode.
// transaction_id_slice - input transaction id (to double-check with transaction id in value). If
//                        empty, decode TransactionId into returned result instead.
// Returned DecodedIntentValue will have a Nil transaction_id unless transaction_id_slice was
// non-null.
Result<DecodedIntentValue> DecodeIntentValue(
    const Slice& encoded_intent_value, const Slice* transaction_id_slice = nullptr);

// Decodes transaction ID from intent value. Consumes it from intent_value slice.
Result<TransactionId> DecodeTransactionIdFromIntentValue(Slice* intent_value);

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
