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

#pragma once

#include <ostream>
#include <string>

#include <boost/function.hpp>

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/dockv/dockv_fwd.h"

#include "yb/util/result.h"
#include "yb/util/slice.h"

namespace yb {

class RefCntPrefix;

namespace dockv {

// We may write intents with empty groups to intents_db, but when interacting with SharedLockManager
// or WaitQueue, we expect no kGroupEnd markers in keys. This method normalizes the passed in key to
// the format expected by conflict resolution. Returns an error if the provided key begins with a
// kGroupEnd marker.
Status RemoveGroupEndSuffix(RefCntPrefix* key);

// "Intent types" are used for single-tablet operations and cross-shard transactions. For example,
// multiple write-only operations don't need to conflict. However, if one operation is a
// read-modify-write snapshot isolation operation, then a write-only operation cannot proceed in
// parallel with it. Conflicts between intent types are handled according to the conflict matrix at
// https://goo.gl/Wbc663.

// "Weak" intents are obtained for prefix SubDocKeys of a key that a transaction is working with.
// E.g. if we're writing "a.b.c", we'll obtain weak write intents on "a" and "a.b", but a strong
// write intent on "a.b.c".
constexpr int kWeakIntentFlag         = 0b000;

// "Strong" intents are obtained on the fully qualified SubDocKey that an operation is working with.
// See the example above.
constexpr int kStrongIntentFlag       = 0b010;

constexpr int kReadIntentFlag         = 0b000;
constexpr int kWriteIntentFlag        = 0b001;

// We put weak intents before strong intents to be able to skip weak intents while checking for
// conflicts.
//
// This was not always the case.
// kObsoleteIntentTypeSet corresponds to intent type set values stored in such a way that
// strong/weak and read/write bits are swapped compared to the current format.
YB_DEFINE_ENUM(IntentType,
    ((kWeakRead,      kWeakIntentFlag |  kReadIntentFlag))
    ((kWeakWrite,     kWeakIntentFlag | kWriteIntentFlag))
    ((kStrongRead,  kStrongIntentFlag |  kReadIntentFlag))
    ((kStrongWrite, kStrongIntentFlag | kWriteIntentFlag))
);

constexpr int kIntentTypeSetMapSize = 1 << kIntentTypeMapSize;
typedef EnumBitSet<IntentType> IntentTypeSet;

// DecodeIntentKey result.
// intent_prefix - intent prefix (SubDocKey (no HT)).
struct DecodedIntentKey {
  Slice intent_prefix;
  IntentTypeSet intent_types;
  EncodedDocHybridTime doc_ht;

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
// require_write_id     - If true, require a write_id in the value and return an error if not found.
// Returned DecodedIntentValue will have a Nil transaction_id unless transaction_id_slice was
// non-null, and will have write_id set as long as one was found.
Result<DecodedIntentValue> DecodeIntentValue(
    const Slice& encoded_intent_value, const Slice* transaction_id_slice = nullptr,
    bool require_write_id = true);

// Decodes transaction ID from intent value. Consumes it from intent_value slice.
Result<TransactionId> DecodeTransactionIdFromIntentValue(Slice* intent_value);

struct ReadIntentTypeSets {
  IntentTypeSet read;
  IntentTypeSet row_mark;
};

[[nodiscard]] ReadIntentTypeSets GetIntentTypesForRead(IsolationLevel level, RowMarkType row_mark);

[[nodiscard]] IntentTypeSet GetIntentTypesForWrite(IsolationLevel level);

YB_STRONGLY_TYPED_BOOL(IsRowLock);

[[nodiscard]] inline IntentTypeSet GetIntentTypes(
    const ReadIntentTypeSets& intents, IsRowLock is_row_lock) {
  return is_row_lock ? intents.row_mark : intents.read;
}

[[nodiscard]] inline IntentTypeSet MakeWeak(IntentTypeSet inp) {
  static constexpr auto kWeakIntentMask = (1 << kStrongIntentFlag) - 1;

  const auto value = inp.ToUIntPtr();
  return IntentTypeSet((value >> kStrongIntentFlag) | (value & kWeakIntentMask));
}

bool HasStrong(IntentTypeSet inp);

IntentTypeSet ObsoleteIntentTypeToSet(uint8_t obsolete_intent_type);
IntentTypeSet ObsoleteIntentTypeSetToNew(uint8_t obsolete_intent_type_set);

// Returns true if ch is value type of one of intent types, obsolete or not.
bool IntentValueType(char ch);

YB_STRONGLY_TYPED_BOOL(LastKey);

// Enumerates intents corresponding to provided key value pairs.
// functor should accept 4 arguments:
// ancestor_doc_key - indicates that doc key is an ancestor of provided key
// full_doc_key - indicates that doc key does not omit any final range components
// value_slice - value of intent
// key - pointer to key in format of SubDocKey (no ht)
// last_key - whether it is the last leaf (non-ancestor) key in enumeration

// Indicates that doc key is an ancestor of provided key or key it self.
YB_STRONGLY_TYPED_BOOL(AncestorDocKey);

// Indicates that the intent contains a full document key, i.e. it does not omit any final range
// components of the document key. This flag is also true for intents that include subdocument keys.
YB_STRONGLY_TYPED_BOOL(FullDocKey);

// TODO(dtxn) don't expose this method outside of DocDB if TransactionConflictResolver is moved
// inside DocDB.
// Note: From https://stackoverflow.com/a/17278470/461529:
// "As of GCC 4.8.1, the std::function in libstdc++ optimizes only for pointers to functions and
// methods. So regardless the size of your functor (lambdas included), initializing a std::function
// from it triggers heap allocation."
// So, we use boost::function which doesn't have such issue:
// http://www.boost.org/doc/libs/1_65_1/doc/html/function/misc.html
using EnumerateIntentsCallback = boost::function<
    Status(AncestorDocKey, FullDocKey, Slice, KeyBytes*, LastKey, IsRowLock)>;

Status EnumerateIntents(
    Slice key, Slice intent_value, const EnumerateIntentsCallback& functor,
    KeyBytes* encoded_key_buffer, PartialRangeKeyIntents partial_range_key_intents,
    LastKey last_key = LastKey::kFalse);

}  // namespace dockv
}  // namespace yb
