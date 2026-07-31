// Copyright (c) YugabyteDB, Inc.
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

#include <boost/preprocessor.hpp>
#include <boost/preprocessor/arithmetic/dec.hpp>
#include <boost/preprocessor/control/expr_iif.hpp>
#include <boost/preprocessor/control/iif.hpp>
#include <boost/preprocessor/logical/bool.hpp>
#include <boost/preprocessor/punctuation/is_begin_parens.hpp>
#include <boost/preprocessor/repetition/for.hpp>
#include <boost/preprocessor/seq/elem.hpp>
#include <boost/preprocessor/seq/size.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <boost/preprocessor/tuple/to_seq.hpp>
#include <boost/preprocessor/variadic/elem.hpp>
#include <cstdint>
#include <string>
#include <vector>
#include <atomic>
#include <functional>
#include <memory>

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.h"
#include "yb/docdb/lock_batch.h"
#include "yb/util/result.h"
#include "yb/util/strongly_typed_bool.h"
#include "yb/common/opid.h"
#include "yb/dockv/dockv_fwd.h"
#include "yb/util/monotime.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"
#include "yb/util/tostring.h"

namespace rocksdb {
class DB;
class WriteBatch;
}  // namespace rocksdb

// DocDB mapping on top of the key-value map in RocksDB:
// <document_key> <hybrid_time> -> <doc_type>
// <document_key> <hybrid_time> <key_a> <gen_ts_a> -> <subdoc_a_type_or_value>
//
// Assuming the type of subdocument corresponding to key_a in the above example is "object", the
// contents of that subdocument are stored in a similar way:
// <document_key> <hybrid_time> <key_a> <gen_ts_a> <key_aa> <gen_ts_aa> -> <subdoc_aa_type_or_value>
// <document_key> <hybrid_time> <key_a> <gen_ts_a> <key_ab> <gen_ts_ab> -> <subdoc_ab_type_or_value>
// ...
//
// See doc_key.h for the encoding of the <document_key> part.
//
// <key_a>, <key_aa> are subkeys indicating a path inside a document.
// Their encoding is as follows:
//   <value_type> -- one byte, see the ValueType enum.
//   <value_specific_encoding> -- e.g. a big-endian 8-byte integer, or a string in a "zero encoded"
//                                format. This is empty for null or true/false values.
//
// <hybrid_time>, <gen_ts_a>, <gen_ts_ab> are "generation hybrid_times" corresponding to hybrid
// clock hybrid_times of the last time a particular top-level document / subdocument was fully
// overwritten or deleted.
//
// <subdoc_a_type_or_value>, <subdoc_aa_type_or_value>, <subdoc_ab_type_or_value> are values of the
// following form:
//   - One-byte value type (see the ValueType enum).
//   - For primitive values, the encoded value. Note: the value encoding may be different from the
//     key encoding for the same data type. E.g. we only flip the sign bit for signed 64-bit
//     integers when encoded as part of a RocksDB key, not value.
//
// Also see this document for a high-level overview of how we lay out JSON documents on top of
// RocksDB:
// https://docs.google.com/document/d/1uEOHUqGBVkijw_CGD568FMt8UOJdHtiE3JROUOppYBU/edit

namespace yb {
class ScopedRWOperation;
enum IsolationLevel : int;

namespace dockv {
class KeyBytes;
}  // namespace dockv
namespace tablet {
class TabletMetricsHolder;
}  // namespace tablet
template <class Entry> class ArenaList;
enum RowMarkType : int;

namespace docdb {
class DocOperation;
class KeyValueWriteBatchPB;
class LWKeyValuePairPB;
class LWKeyValueWriteBatchPB;
class SchemaPackingProvider;
class SharedLockManager;
enum class InitMarkerBehavior;
struct DocDB;
struct KeyBounds;
struct ReadOperationData;

// This function prepares the transaction by taking locks. The set of keys locked are returned to
// the caller via the keys_locked argument (because they need to be saved and unlocked when the
// transaction commits). A flag is also returned to indicate if any of the write operations
// requires a clean read snapshot to be taken before being applied (see DocOperation for details).
//
// Example: doc_write_ops might consist of the following operations:
// a.b = {}, a.b.c = 1, a.b.d = 2, e.d = 3
// We will generate all the lock_prefixes for the keys with lock types
// a - shared, a.b - exclusive, a - shared, a.b - shared, a.b.c - exclusive ...
// Then we will deduplicate the keys and promote shared locks to exclusive, and sort them.
// Finally, the locks taken will be in order:
// a - shared, a.b - exclusive, a.b.c - exclusive, a.b.d - exclusive, e - shared, e.d - exclusive.
// Then the sorted lock key list will be returned. (Type is not returned because it is not needed
// for unlocking)
// TODO(akashnil): If a.b is exclusive, we don't need to lock any sub-paths under it.
//
// Input: doc_write_ops
// Context: lock_manager

struct PrepareDocWriteOperationResult {
  LockBatch lock_batch;
  bool need_read_snapshot = false;
};

Result<PrepareDocWriteOperationResult> PrepareDocWriteOperation(
    const std::vector<std::unique_ptr<DocOperation>>& doc_write_ops,
    const ArenaList<LWKeyValuePairPB>& read_pairs,
    const std::shared_ptr<tablet::TabletMetricsHolder>& tablet_metrics,
    IsolationLevel isolation_level,
    RowMarkType row_mark_type,
    bool transactional_table,
    bool write_transaction_metadata,
    CoarseTimePoint deadline,
    dockv::PartialRangeKeyIntents partial_range_key_intents,
    SharedLockManager *lock_manager,
    dockv::SkipPrefixLocks skip_prefix_locks = dockv::SkipPrefixLocks::kFalse);

// This constructs a DocWriteBatch using the given list of DocOperations, reading the previous
// state of data from RocksDB when necessary.
//
// Input: doc_write_ops, read snapshot hybrid_time if requested in PrepareDocWriteOperation().
// Context: rocksdb
// Outputs: keys_locked, write_batch
Status AssembleDocWriteBatch(
    const std::vector<std::unique_ptr<DocOperation>>& doc_write_ops,
    const ReadOperationData& read_operation_data,
    const DocDB& doc_db,
    SchemaPackingProvider* schema_packing_provider /* null okay */,
    std::reference_wrapper<const ScopedRWOperation> pending_op,
    LWKeyValueWriteBatchPB* write_batch,
    InitMarkerBehavior init_marker_behavior,
    std::atomic<int64_t>* monotonic_counter,
    ReadRestartData* read_restart_data,
    const std::string& table_name);

// replicated_batches_state format does not matter at this point, because it is just
// appended to appropriate value.
void PrepareTransactionWriteBatch(
    const docdb::KeyValueWriteBatchPB& put_batch,
    HybridTime hybrid_time,
    rocksdb::WriteBatch* rocksdb_write_batch,
    const TransactionId& transaction_id,
    IsolationLevel isolation_level,
    dockv::PartialRangeKeyIntents partial_range_key_intents,
    const Slice& replicated_batches_state,
    IntraTxnWriteId* write_id);


struct IntentKeyValueForCDC {
  Slice key;
  Slice value;
  Slice ht;
  std::string key_buf, value_buf, ht_buf;
  std::string reverse_index_key;
  DocHybridTime intent_ht;
  IntraTxnWriteId write_id = 0;

  std::string ToString() const;

  template <class PB>
  void ToPB(PB* pb) const {
    pb->set_key(key);
    pb->set_value(value);
    pb->set_reverse_index_key(reverse_index_key);
    pb->set_write_id(write_id);
  }

  template <class PB>
  static IntentKeyValueForCDC FromPB(const PB& pb) {
    return IntentKeyValueForCDC {
        .key = pb.key(),
        .value = pb.value(),
        .reverse_index_key = pb.reverse_index_key(),
        .write_id = pb.write_id(),
    };
  }
};

// See ApplyTransactionStatePB for details.
struct ApplyTransactionState {
  std::string key;
  IntraTxnWriteId write_id = 0;
  SubtxnSet aborted;

  bool active() const {
    return !key.empty();
  }

  std::string ToString() const;

  template <class PB>
  void ToPB(PB* pb) const {
    pb->set_key(key);
    pb->set_write_id(write_id);
    aborted.ToPB(pb->mutable_aborted()->mutable_set());
  }

  template <class PB>
  static Result<ApplyTransactionState> FromPB(const PB& pb) {
    return ApplyTransactionState {
      .key = pb.key(),
      .write_id = pb.write_id(),
      .aborted = VERIFY_RESULT(SubtxnSet::FromPB(pb.aborted().set())),
    };
  }
};

struct ApplyStateWithCommitInfo {
  ApplyTransactionState state;
  HybridTime commit_ht;
  OpId apply_op_id;

  template <class PB>
  static Result<ApplyStateWithCommitInfo> FromPB(const PB& pb) {
    return ApplyStateWithCommitInfo {
      .state = VERIFY_RESULT(ApplyTransactionState::FromPB(pb)),
      .commit_ht = HybridTime(pb.commit_ht()),
      .apply_op_id = OpId::FromPB(pb.apply_op_id()),
    };
  }

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(state, commit_ht, apply_op_id);
  }
};

Result<ApplyTransactionState> GetIntentsBatchForCDC(
    const TransactionId& transaction_id,
    const KeyBounds* key_bounds,
    const ApplyTransactionState* stream_state,
    const SubtxnSet& aborted,
    rocksdb::DB* intents_db,
    std::vector<IntentKeyValueForCDC>* keyValueIntents);

void AppendTransactionKeyPrefix(const TransactionId& transaction_id, dockv::KeyBytes* out);

} // namespace docdb
} // namespace yb
