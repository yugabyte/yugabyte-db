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

#include "yb/docdb/conflict_resolution.h"

#include <boost/scope_exit.hpp>

#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/intent.h"
#include "yb/docdb/shared_lock_manager.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/metrics.h"

using namespace std::literals;
using namespace std::placeholders;

namespace yb {
namespace docdb {

namespace {

using TransactionIdSet = std::unordered_set<TransactionId, TransactionIdHash>;

struct TransactionData {
  TransactionId id;
  TransactionStatus status;
  HybridTime commit_time;
  TransactionMetadata metadata;
  Status failure;

  void ProcessStatus(const TransactionStatusResult& result) {
    status = result.status;
    if (status == TransactionStatus::COMMITTED) {
      LOG_IF(DFATAL, !result.status_time.is_valid())
          << "Status time not specified for committed transaction: " << id;
      commit_time = result.status_time;
    }
  }

  std::string ToString() const {
    return Format("{ id: $0 status: $1 commit_time: $2 metadata: $3 failure: $4 }",
                  id, TransactionStatus_Name(status), commit_time, metadata, failure);
  }
};

CHECKED_STATUS MakeConflictStatus(const TransactionId& id, const char* reason,
                                  Counter* conflicts_metric) {
  conflicts_metric->Increment();
  return STATUS_FORMAT(TryAgain,
                       "Conflicts with $0 transaction: $1",
                       reason,
                       FullyDecodeTransactionId(Slice(id.data, id.size())));
}

class ConflictResolver;

class ConflictResolverContext {
 public:
  // Read all conflicts for operation/transaction.
  virtual CHECKED_STATUS ReadConflicts(ConflictResolver* resolver) = 0;

  // Check priority of this one against existing transactions.
  virtual CHECKED_STATUS CheckPriority(
      ConflictResolver* resolver,
      std::vector<TransactionData>* transactions) = 0;

  // Check for conflict against committed transaction.
  virtual CHECKED_STATUS CheckConflictWithCommitted(
      const TransactionId& id, HybridTime commit_time) = 0;

  virtual HybridTime GetHybridTime() = 0;

  virtual bool IgnoreConflictsWith(const TransactionId& other) = 0;

 protected:
  ~ConflictResolverContext() {}
};

class ConflictResolver {
 public:
  ConflictResolver(const DocDB& doc_db,
                   TransactionStatusManager* status_manager,
                   ConflictResolverContext* context)
      : doc_db_(doc_db), status_manager_(*status_manager), request_scope_(status_manager),
        context_(*context) {}

  TransactionStatusManager& status_manager() {
    return status_manager_;
  }

  const DocDB& doc_db() {
    return doc_db_;
  }

  boost::optional<TransactionMetadata> Metadata(const TransactionId& id) {
    return status_manager_.Metadata(id);
  }

  CHECKED_STATUS Resolve() {
    RETURN_NOT_OK(context_.ReadConflicts(this));
    return ResolveConflicts();
  }

  // Reads conflicts for specified intent from DB.
  CHECKED_STATUS ReadIntentConflicts(IntentTypeSet type, KeyBytes* intent_key_prefix) {
    EnsureIntentIteratorCreated();

    const auto conflicting_intent_types = kIntentTypeSetConflicts[type.ToUIntPtr()];

    KeyBytes upperbound_key(*intent_key_prefix);
    upperbound_key.AppendValueType(ValueType::kMaxByte);
    intent_key_upperbound_ = upperbound_key.AsSlice();

    intent_key_prefix->AppendValueType(ValueType::kIntentTypeSet);
    BOOST_SCOPE_EXIT(intent_key_prefix, &intent_key_upperbound_) {
      intent_key_prefix->RemoveValueTypeSuffix(ValueType::kIntentTypeSet);
      intent_key_upperbound_.clear();
    } BOOST_SCOPE_EXIT_END;
    auto prefix_slice = intent_key_prefix->AsSlice();
    prefix_slice.remove_suffix(1);
    intent_iter_->Seek(intent_key_prefix->data());
    while (intent_iter_->Valid()) {
      auto existing_key = intent_iter_->key();
      auto existing_value = intent_iter_->value();
      if (!existing_key.starts_with(prefix_slice)) {
        break;
      }
      // Support for obsolete intent type.
      // When looking for intent with specific prefix it should start with this prefix, followed
      // by ValueType::kIntentTypeSet.
      // Previously we were using intent type, so should support its value type also, now it is
      // kObsoleteIntentType.
      // Actual handling of obsolete intent type is done in ParseIntentKey.
      if (existing_key.size() <= prefix_slice.size() ||
          (existing_key[prefix_slice.size()] != ValueTypeAsChar::kIntentTypeSet &&
           existing_key[prefix_slice.size()] != ValueTypeAsChar::kObsoleteIntentType)) {
        break;
      }
      if (existing_value.empty() || existing_value[0] != ValueTypeAsChar::kTransactionId) {
        return STATUS_FORMAT(Corruption,
            "Transaction prefix expected in intent: $0 => $1",
            existing_key.ToDebugHexString(),
            existing_value.ToDebugHexString());
      }
      existing_value.consume_byte();
      auto existing_intent = VERIFY_RESULT(
          docdb::ParseIntentKey(intent_iter_->key(), existing_value));

      const auto intent_mask = kIntentTypeSetMask[existing_intent.types.ToUIntPtr()];
      if ((conflicting_intent_types & intent_mask) != 0) {
        auto transaction_id = VERIFY_RESULT(FullyDecodeTransactionId(
            Slice(existing_value.data(), TransactionId::static_size())));

        if (!context_.IgnoreConflictsWith(transaction_id)) {
          conflicts_.insert(transaction_id);
        }
      }

      intent_iter_->Next();
    }

    return Status::OK();
  }

 private:
  CHECKED_STATUS ResolveConflicts() {
    if (!conflicts_.empty()) {
      transactions_.reserve(conflicts_.size());
      for (const auto& transaction_id : conflicts_) {
        transactions_.push_back({ transaction_id });
      }

      return DoResolveConflicts();
    }

    return Status::OK();
  }

  void EnsureIntentIteratorCreated() {
    if (!intent_iter_) {
      intent_iter_ = CreateRocksDBIterator(
          doc_db_.intents,
          BloomFilterMode::DONT_USE_BLOOM_FILTER,
          boost::none /* user_key_for_filter */,
          rocksdb::kDefaultQueryId,
          nullptr /* file_filter */,
          &intent_key_upperbound_);
    }
  }

  CHECKED_STATUS DoResolveConflicts() {
    for (;;) {
      RETURN_NOT_OK(CheckLocalCommits());

      FetchTransactionStatuses();

      RETURN_NOT_OK(Cleanup());
      if (transactions_.empty()) {
        return Status::OK();
      }

      RETURN_NOT_OK(context_.CheckPriority(this, &transactions_));

      RETURN_NOT_OK(AbortTransactions());

      RETURN_NOT_OK(Cleanup());

      if (transactions_.empty()) {
        return Status::OK();
      }
    }
  }

  CHECKED_STATUS CheckLocalCommits() {
    auto write_iterator = transactions_.begin();
    for (const auto& transaction : transactions_) {
      auto commit_time = status_manager().LocalCommitTime(transaction.id);
      if (!commit_time.is_valid()) {
        *write_iterator = transaction;
        ++write_iterator;
        continue;
      }
      RETURN_NOT_OK(context_.CheckConflictWithCommitted(transaction.id, commit_time));
    }
    transactions_.erase(write_iterator, transactions_.end());

    return Status::OK();
  }

  // Removes all transactions that would not conflict with us anymore.
  // Returns failure if we conflict with transaction that cannot be aborted.
  CHECKED_STATUS Cleanup() {
    auto write_iterator = transactions_.begin();
    for (const auto& transaction : transactions_) {
      RETURN_NOT_OK(transaction.failure);
      auto status = transaction.status;
      if (status == TransactionStatus::COMMITTED) {
        RETURN_NOT_OK(context_.CheckConflictWithCommitted(transaction.id, transaction.commit_time));
        continue;
      } else if (status == TransactionStatus::ABORTED) {
        auto commit_time = status_manager().LocalCommitTime(transaction.id);
        if (commit_time.is_valid()) {
          RETURN_NOT_OK(context_.CheckConflictWithCommitted(transaction.id, commit_time));
        }
        continue;
      } else {
        DCHECK(TransactionStatus::PENDING == status ||
               TransactionStatus::APPLYING == status)
            << "Actual status: " << TransactionStatus_Name(status);
      }
      *write_iterator = transaction;
      ++write_iterator;
    }
    transactions_.erase(write_iterator, transactions_.end());

    return Status::OK();
  }

  void FetchTransactionStatuses() {
    static const std::string kRequestReason = "conflict resolution"s;
    CountDownLatch latch(transactions_.size());
    for (auto& i : transactions_) {
      auto& transaction = i;
      StatusRequest request = {
        &transaction.id,
        context_.GetHybridTime(),
        context_.GetHybridTime(),
        0, // serial no. Could use 0 here, because read_ht == global_limit_ht.
           // So we cannot accept status with time >= read_ht and < global_limit_ht.
        &kRequestReason,
        MustExist::kTrue,
        [&transaction, &latch](Result<TransactionStatusResult> result) {
          if (result.ok()) {
            transaction.ProcessStatus(*result);
          } else if (result.status().IsTryAgain()) {
            // It is safe to suppose that transaction in PENDING state in case of try again error.
            transaction.status = TransactionStatus::PENDING;
          } else if (result.status().IsNotFound()) {
            transaction.status = TransactionStatus::ABORTED;
          } else {
            transaction.failure = result.status();
          }
          latch.CountDown();
        }
      };
      status_manager().RequestStatusAt(request);
    }
    latch.Wait();
  }

  CHECKED_STATUS AbortTransactions() {
    struct AbortContext {
      size_t left;
      std::mutex mutex;
      std::condition_variable cond;
      Status result;
    };
    AbortContext context{ transactions_.size() };
    for (auto& i : transactions_) {
      auto& transaction = i;
      status_manager().Abort(
          transaction.id,
          [&transaction, &context](Result<TransactionStatusResult> result) {
            std::lock_guard<std::mutex> lock(context.mutex);
            if (result.ok()) {
              transaction.ProcessStatus(*result);
            } else if (result.status().IsRemoteError()) {
              context.result = result.status();
            } else {
              LOG(INFO) << "Abort failed, would retry: " << result.status();
            }
            if (--context.left == 0) {
              context.cond.notify_one();
            }
      });
    }
    std::unique_lock<std::mutex> lock(context.mutex);
    context.cond.wait(lock, [&context] { return context.left == 0; });
    return context.result;
  }

  DocDB doc_db_;
  std::unique_ptr<rocksdb::Iterator> intent_iter_;
  Slice intent_key_upperbound_;
  TransactionStatusManager& status_manager_;
  RequestScope request_scope_;
  ConflictResolverContext& context_;
  TransactionIdSet conflicts_;
  std::vector<TransactionData> transactions_;
};

// Utility class for ResolveTransactionConflicts implementation.
class TransactionConflictResolverContext : public ConflictResolverContext {
 public:
  TransactionConflictResolverContext(const DocOperations& doc_ops,
                                     const KeyValueWriteBatchPB& write_batch,
                                     HybridTime hybrid_time,
                                     Counter* conflicts_metric)
      : doc_ops_(doc_ops),
        write_batch_(write_batch),
        hybrid_time_(hybrid_time),
        transaction_id_(FullyDecodeTransactionId(
            write_batch.transaction().transaction_id())),
        conflicts_metric_(conflicts_metric)
  {}

  virtual ~TransactionConflictResolverContext() {}

 private:
  CHECKED_STATUS ReadConflicts(ConflictResolver* resolver) override {
    RETURN_NOT_OK(transaction_id_);

    VLOG(4) << "Resolve conflicts: " << transaction_id_;

    if (write_batch_.transaction().has_isolation()) {
      metadata_ = VERIFY_RESULT(TransactionMetadata::FromPB(write_batch_.transaction()));
    } else {
      // If write request does not contain metadata it means that metadata is stored in
      // local cache.
      auto stored_metadata = resolver->Metadata(*transaction_id_);
      if (!stored_metadata) {
        return STATUS_FORMAT(IllegalState, "Unknown transaction: $0", *transaction_id_);
      }
      metadata_ = std::move(*stored_metadata);
    }

    boost::container::small_vector<RefCntPrefix, 8> paths;

    KeyBytes encoded_key_buffer;
    EnumerateIntentsCallback callback = std::bind(
        &TransactionConflictResolverContext::ProcessIntent, this, resolver,
        GetStrongIntentTypeSet(metadata_.isolation, docdb::OperationKind::kWrite), _1, _3);
    for (const auto& doc_op : doc_ops_) {
      paths.clear();
      IsolationLevel ignored_isolation_level;
      RETURN_NOT_OK(doc_op->GetDocPaths(
          GetDocPathsMode::kIntents, &paths, &ignored_isolation_level));

      for (const auto& path : paths) {
        RETURN_NOT_OK(EnumerateIntents(path.as_slice(), Slice(), callback, &encoded_key_buffer));
      }
    }

    RETURN_NOT_OK(DoReadConflicts(
        write_batch_.read_pairs(), docdb::OperationKind::kRead, resolver));

    return Status::OK();
  }

  CHECKED_STATUS DoReadConflicts(
      const google::protobuf::RepeatedPtrField<docdb::KeyValuePairPB>& pairs,
      docdb::OperationKind kind,
      ConflictResolver* resolver) {
    if (pairs.empty()) {
      return Status::OK();
    }

    return EnumerateIntents(
        pairs,
        std::bind(&TransactionConflictResolverContext::ProcessIntent, this, resolver,
                  GetStrongIntentTypeSet(metadata_.isolation, kind), _1, _3));
  }

  // Processes intent generated by EnumerateIntents.
  // I.e. fetches conflicting intents and fills list of conflicting transactions.
  CHECKED_STATUS ProcessIntent(ConflictResolver* resolver,
                               IntentTypeSet strong_intent_types,
                               IntentStrength strength,
                               KeyBytes* intent_key_prefix) {
    auto intent_type_set = strength == IntentStrength::kWeak
        ? StrongToWeak(strong_intent_types) : strong_intent_types;

    if (strength == IntentStrength::kStrong &&
        metadata_.isolation == IsolationLevel::SNAPSHOT_ISOLATION) {
      Slice key_slice(intent_key_prefix->data());

      auto value_iter = CreateRocksDBIterator(
          resolver->doc_db().regular,
          BloomFilterMode::USE_BLOOM_FILTER,
          key_slice,
          rocksdb::kDefaultQueryId);

      value_iter->Seek(key_slice);
      if (value_iter->Valid() && value_iter->key().starts_with(key_slice)) {
        auto existing_key = value_iter->key();
        DocHybridTime doc_ht;
        RETURN_NOT_OK(doc_ht.DecodeFromEnd(existing_key));
        if (doc_ht.hybrid_time() >= metadata_.start_time) {
          conflicts_metric_->Increment();
          return STATUS_FORMAT(TryAgain, "Value write after transaction start: $0 >= $1",
                               doc_ht.hybrid_time(), metadata_.start_time);
        }
      }
    }

    return resolver->ReadIntentConflicts(intent_type_set, intent_key_prefix);
  }

  CHECKED_STATUS CheckPriority(ConflictResolver* resolver,
                               std::vector<TransactionData>* transactions) override {
    auto our_priority = metadata_.priority;
    for (auto& transaction : *transactions) {
      if (!fetched_metadata_for_transactions_) {
        auto their_metadata = resolver->Metadata(transaction.id);
        if (!their_metadata) {
          // This should not really happen.
          return STATUS_FORMAT(IllegalState,
                               "Does not have metadata for conflicting transaction: $0",
                               transaction.id);
        }
        transaction.metadata = std::move(*their_metadata);
      }
      auto their_priority = transaction.metadata.priority;
      if (our_priority < their_priority) {
        return MakeConflictStatus(transaction.id, "higher priority", conflicts_metric_);
      }
    }
    fetched_metadata_for_transactions_ = true;

    return Status::OK();
  }

  CHECKED_STATUS CheckConflictWithCommitted(
      const TransactionId& id, HybridTime commit_time) override {
    DSCHECK(commit_time.is_valid(), Corruption, "Invalid transaction commit time");
    DSCHECK(metadata_.start_time.is_valid(), Corruption, "Invalid transaction start time");

    VLOG(4) << "Committed: " << id << ", " << commit_time;

    bool conflicts;
    if (metadata_.isolation == IsolationLevel::SNAPSHOT_ISOLATION) {
      conflicts = commit_time >= metadata_.start_time; // TODO(dtxn) clock skew?
    } else {
      // HybridTime::kMax means that transaction is not actually committed, but is going to commit.
      // So we should conflict with such transaction, because we are not able to read its results.
      conflicts = commit_time == HybridTime::kMax;
    }

    if (conflicts) {
      return MakeConflictStatus(id, "committed", conflicts_metric_);
    }

    return Status::OK();
  }

  HybridTime GetHybridTime() override {
    return hybrid_time_;
  }

  bool IgnoreConflictsWith(const TransactionId& other) override {
    return other == *transaction_id_;
  }

  const DocOperations& doc_ops_;
  const KeyValueWriteBatchPB& write_batch_;
  HybridTime hybrid_time_;
  Result<TransactionId> transaction_id_;
  TransactionMetadata metadata_;
  Status result_ = Status::OK();
  bool fetched_metadata_for_transactions_ = false;
  Counter* conflicts_metric_ = nullptr;
};

class OperationConflictResolverContext : public ConflictResolverContext {
 public:
  OperationConflictResolverContext(const DocOperations* doc_ops,
                                   HybridTime hybrid_time)
      : doc_ops_(*doc_ops), hybrid_time_(hybrid_time) {
  }

  virtual ~OperationConflictResolverContext() {}

  // Reads stored intents that could conflict with our operations.
  CHECKED_STATUS ReadConflicts(ConflictResolver* resolver) override {
    boost::container::small_vector<RefCntPrefix, 8> doc_paths;
    boost::container::small_vector<size_t, 32> key_prefix_lengths;
    KeyBytes current_intent_prefix;

    for (const auto& doc_op : doc_ops_) {
      doc_paths.clear();
      IsolationLevel isolation;
      RETURN_NOT_OK(doc_op->GetDocPaths(GetDocPathsMode::kIntents, &doc_paths, &isolation));

      auto strong_intent_types = GetStrongIntentTypeSet(isolation, OperationKind::kWrite);

      for (const auto& doc_path : doc_paths) {
        key_prefix_lengths.clear();
        RETURN_NOT_OK(SubDocKey::DecodePrefixLengths(doc_path.as_slice(), &key_prefix_lengths));
        current_intent_prefix.Clear();
        for (auto it = key_prefix_lengths.begin(), end = key_prefix_lengths.end(); it != end;) {
          current_intent_prefix.AppendRawBytes(
              doc_path.data() + current_intent_prefix.size(),
              *it - current_intent_prefix.size());
          ++it;
          RETURN_NOT_OK(resolver->ReadIntentConflicts(
              it == end ? strong_intent_types : StrongToWeak(strong_intent_types),
              &current_intent_prefix));
        }
      }
    }

    return Status::OK();
  }

  CHECKED_STATUS CheckPriority(ConflictResolver*, std::vector<TransactionData>*) override {
    return Status::OK();
  }

  HybridTime GetHybridTime() override {
    return hybrid_time_;
  }

  bool IgnoreConflictsWith(const TransactionId& other) override {
    return false;
  }

  CHECKED_STATUS CheckConflictWithCommitted(
      const TransactionId& id, HybridTime commit_time) override {
    hybrid_time_.MakeAtLeast(commit_time);
    return Status::OK();
  }

 private:
  const DocOperations& doc_ops_;
  HybridTime hybrid_time_;
};

} // namespace

Status ResolveTransactionConflicts(const DocOperations& doc_ops,
                                   const KeyValueWriteBatchPB& write_batch,
                                   HybridTime hybrid_time,
                                   const DocDB& doc_db,
                                   TransactionStatusManager* status_manager,
                                   Counter* conflicts_metric) {
  DCHECK(hybrid_time.is_valid());
  TransactionConflictResolverContext context(
      doc_ops, write_batch, hybrid_time, conflicts_metric);
  ConflictResolver resolver(doc_db, status_manager, &context);
  return resolver.Resolve();
}

Result<HybridTime> ResolveOperationConflicts(const DocOperations& doc_ops,
                                             HybridTime hybrid_time,
                                             const DocDB& doc_db,
                                             TransactionStatusManager* status_manager) {
  OperationConflictResolverContext context(&doc_ops, hybrid_time);
  ConflictResolver resolver(doc_db, status_manager, &context);
  RETURN_NOT_OK(resolver.Resolve());
  return context.GetHybridTime();
}

#define INTENT_KEY_SCHECK(lhs, op, rhs, msg) \
  BOOST_PP_CAT(SCHECK_, op)(lhs, \
                            rhs, \
                            Corruption, \
                            Format("Bad intent key, $0 in $1, transaction from: $2", \
                                   msg, \
                                   intent_key.ToDebugHexString(), \
                                   transaction_id_source.ToDebugHexString()))

// transaction_id_slice used in INTENT_KEY_SCHECK
Result<ParsedIntent> ParseIntentKey(Slice intent_key, Slice transaction_id_source) {
  ParsedIntent result;
  int doc_ht_size = 0;
  result.doc_path = intent_key;
  // Intent is encoded as "DocPath + IntentType + DocHybridTime".
  RETURN_NOT_OK(DocHybridTime::CheckAndGetEncodedSize(result.doc_path, &doc_ht_size));
  // 3 comes from (ValueType::kIntentType, the actual intent type, ValueType::kHybridTime).
  INTENT_KEY_SCHECK(result.doc_path.size(), GE, doc_ht_size + 3, "key too short");
  result.doc_path.remove_suffix(doc_ht_size + 3);
  auto intent_type_and_doc_ht = result.doc_path.end();
  if (intent_type_and_doc_ht[0] != ValueTypeAsChar::kObsoleteIntentType) {
    INTENT_KEY_SCHECK(intent_type_and_doc_ht[0], EQ, ValueTypeAsChar::kIntentTypeSet,
        "intent type set type expected");
    result.types = IntentTypeSet(intent_type_and_doc_ht[1]);
  } else {
    result.types = ObsoleteIntentTypeToSet(intent_type_and_doc_ht[1]);
  }
  INTENT_KEY_SCHECK(intent_type_and_doc_ht[2], EQ, ValueTypeAsChar::kHybridTime,
                    "hybrid time value type expected");
  result.doc_ht = Slice(result.doc_path.end() + 2, doc_ht_size + 1);
  return result;
}

std::string DebugIntentKeyToString(Slice intent_key) {
  auto parsed = ParseIntentKey(intent_key, Slice());
  if (!parsed.ok()) {
    LOG(WARNING) << "Failed to parse: " << intent_key.ToDebugHexString() << ": " << parsed.status();
    return intent_key.ToDebugHexString();
  }
  DocHybridTime doc_ht;
  auto status = doc_ht.DecodeFromEnd(parsed->doc_ht);
  if (!status.ok()) {
    LOG(WARNING) << "Failed to decode doc ht: " << intent_key.ToDebugHexString() << ": " << status;
    return intent_key.ToDebugHexString();
  }
  return Format("$0 (key: $1 type: $2 doc_ht: $3 )",
                intent_key.ToDebugHexString(),
                SubDocKey::DebugSliceToString(parsed->doc_path),
                parsed->types,
                doc_ht.ToString());
}

} // namespace docdb
} // namespace yb
