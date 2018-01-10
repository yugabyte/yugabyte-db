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
      commit_time = result.status_time;
    }
  }
};

CHECKED_STATUS MakeConflictStatus(const TransactionId& id, const char* reason) {
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

 protected:
  ~ConflictResolverContext() {}
};

class ConflictResolver {
 public:
  ConflictResolver(rocksdb::DB* db,
                   TransactionStatusManager* status_manager,
                   ConflictResolverContext* context)
    : db_(db), status_manager_(*status_manager), context_(*context) {}

  TransactionStatusManager& status_manager() {
    return status_manager_;
  }

  rocksdb::DB* db() {
    return db_;
  }

  boost::optional<TransactionMetadata> Metadata(const TransactionId& id) {
    return status_manager_.Metadata(id);
  }

  CHECKED_STATUS Resolve() {
    RETURN_NOT_OK(context_.ReadConflicts(this));
    return ResolveConflicts();
  }

  // Reads conflicts for specified intent from DB.
  CHECKED_STATUS ReadIntentConflicts(IntentType type, KeyBytes* intent_key_prefix) {
    EnsureIntentIteratorCreated();

    const auto& conflicting_intent_types = kIntentConflicts[static_cast<size_t>(type)];

    intent_key_prefix->AppendValueType(ValueType::kIntentType);
    BOOST_SCOPE_EXIT(intent_key_prefix) {
      intent_key_prefix->RemoveValueTypeSuffix(ValueType::kIntentType);
    } BOOST_SCOPE_EXIT_END;
    intent_iter_->Seek(intent_key_prefix->data());
    while (intent_iter_->Valid()) {
      auto existing_key = intent_iter_->key();
      auto existing_value = intent_iter_->value();
      if (!existing_key.starts_with(intent_key_prefix->data())) {
        break;
      }
      if (existing_value.empty() ||
          existing_value[0] != static_cast<uint8_t>(ValueType::kTransactionId)) {
        return STATUS_FORMAT(Corruption,
            "Transaction prefix expected in intent: $0 => $1",
            existing_key.ToDebugHexString(),
            existing_value.ToDebugHexString());
      }
      existing_value.consume_byte();
      auto existing_intent = docdb::ParseIntentKey(intent_iter_->key(), existing_value);
      RETURN_NOT_OK(existing_intent);

      if (conflicting_intent_types.test(static_cast<size_t>(existing_intent->type))) {
        auto transaction_id = FullyDecodeTransactionId(
            Slice(existing_value.data(), TransactionId::static_size()));
        RETURN_NOT_OK(transaction_id);

        conflicts_.insert(*transaction_id);
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
          db_,
          BloomFilterMode::DONT_USE_BLOOM_FILTER,
          boost::none /* user_key_for_filter */,
          rocksdb::kDefaultQueryId);
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

      AbortTransactions();

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
    CountDownLatch latch(transactions_.size());
    for (auto& i : transactions_) {
      auto& transaction = i;
      StatusRequest request = {
        &transaction.id,
        context_.GetHybridTime(),
        context_.GetHybridTime(),
        0, // serial no. Could use 0 here, because read_ht == global_limit_ht.
           // So we cannot accept status with time >= read_ht and < global_limit_ht.
        [&transaction, &latch](Result<TransactionStatusResult> result) {
          if (result.ok()) {
            transaction.ProcessStatus(*result);
          } else if (result.status().IsTryAgain()) {
            // It is safe to suppose that transaction in PENDING state in case of try again error.
            transaction.status = TransactionStatus::PENDING;
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

  void AbortTransactions() {
    CountDownLatch latch(transactions_.size());
    for (auto& i : transactions_) {
      auto& transaction = i;
      status_manager().Abort(
          transaction.id,
          [&transaction, &latch](Result<TransactionStatusResult> result) {
            if (result.ok()) {
              transaction.ProcessStatus(*result);
            } else {
              LOG(INFO) << "Abort failed, would retry: " << result.status();
            }
            latch.CountDown();
      });
    }
    latch.Wait();
  }

  rocksdb::DB* db_;
  std::unique_ptr<rocksdb::Iterator> intent_iter_;
  TransactionStatusManager& status_manager_;
  ConflictResolverContext& context_;
  TransactionIdSet conflicts_;
  std::vector<TransactionData> transactions_;
};

// Utility class for ResolveTransactionConflicts implementation.
class TransactionConflictResolverContext : public ConflictResolverContext {
 public:
  TransactionConflictResolverContext(const KeyValueWriteBatchPB& write_batch,
                                     HybridTime hybrid_time)
      : write_batch_(write_batch),
        hybrid_time_(hybrid_time),
        transaction_id_(FullyDecodeTransactionId(
            write_batch.transaction().transaction_id()))
  {}

  virtual ~TransactionConflictResolverContext() {}

 private:
  CHECKED_STATUS ReadConflicts(ConflictResolver* resolver) override {
    RETURN_NOT_OK(transaction_id_);

    if (write_batch_.transaction().has_isolation()) {
      auto converted_metadata = TransactionMetadata::FromPB(write_batch_.transaction());
      RETURN_NOT_OK(converted_metadata);
      metadata_ = std::move(*converted_metadata);
    } else {
      // If write request does not contain metadata it means that metadata is stored in
      // local cache.
      auto stored_metadata = resolver->Metadata(*transaction_id_);
      if (!stored_metadata) {
        return STATUS_FORMAT(IllegalState, "Unknown transaction: $0", *transaction_id_);
      }
      metadata_ = std::move(*stored_metadata);
    }

    intent_types_ = GetWriteIntentsForIsolationLevel(metadata_.isolation);

    return EnumerateIntents(
        write_batch_.kv_pairs(),
        std::bind(&TransactionConflictResolverContext::ProcessIntent, this, resolver, _1, _3));
  }

  // Processes intent generated by EnumerateIntents.
  // I.e. fetches conflicting intents and fills list of conflicting transactions.
  CHECKED_STATUS ProcessIntent(ConflictResolver* resolver,
                               IntentKind kind,
                               KeyBytes* intent_key_prefix) {
    auto intent_type = intent_types_[kind];

    if (kind == IntentKind::kStrong &&
        metadata_.isolation == IsolationLevel::SNAPSHOT_ISOLATION) {
      Slice key_slice(intent_key_prefix->data());
      DCHECK_EQ(ValueType::kIntentPrefix, static_cast<ValueType>(key_slice[0]));
      key_slice.consume_byte();

      auto value_iter = CreateRocksDBIterator(
          resolver->db(),
          BloomFilterMode::USE_BLOOM_FILTER,
          key_slice,
          rocksdb::kDefaultQueryId);

      value_iter->Seek(key_slice);
      if (value_iter->Valid() && value_iter->key().starts_with(key_slice)) {
        auto existing_key = value_iter->key();
        DocHybridTime doc_ht;
        RETURN_NOT_OK(doc_ht.DecodeFromEnd(existing_key));
        if (doc_ht.hybrid_time() >= metadata_.start_time) {
          return STATUS(TryAgain, "Value write after transaction start");
        }
      }
    }

    return resolver->ReadIntentConflicts(intent_type, intent_key_prefix);
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
        return MakeConflictStatus(transaction.id, "higher priority");
      }
    }
    fetched_metadata_for_transactions_ = true;

    return Status::OK();
  }

  CHECKED_STATUS CheckConflictWithCommitted(
      const TransactionId& id, HybridTime commit_time) override {
    if (metadata_.isolation == yb::IsolationLevel::SNAPSHOT_ISOLATION) {
      if (commit_time >= metadata_.start_time) { // TODO(dtxn) clock skew?
        return MakeConflictStatus(id, "committed");
      }
    }
    return Status::OK();
  }

  HybridTime GetHybridTime() override {
    return hybrid_time_;
  }

  const KeyValueWriteBatchPB& write_batch_;
  HybridTime hybrid_time_;
  Result<TransactionId> transaction_id_;
  TransactionMetadata metadata_;
  IntentTypePair intent_types_;
  Status result_ = Status::OK();
  bool fetched_metadata_for_transactions_ = false;
};

class OperationConflictResolverContext : public ConflictResolverContext {
 public:
  OperationConflictResolverContext(const DocOperations* doc_ops,
                                   HybridTime hybrid_time)
      : doc_ops_(*doc_ops), hybrid_time_(hybrid_time) {
  }

  virtual ~OperationConflictResolverContext() {}

  // Reads stored intents, that could conflict with our operations.
  CHECKED_STATUS ReadConflicts(ConflictResolver* resolver) override {
    std::list<DocPath> doc_paths;
    KeyBytes current_intent_prefix;

    for (const auto& doc_op : doc_ops_) {
      doc_paths.clear();
      IsolationLevel isolation;
      doc_op->GetDocPathsToLock(&doc_paths, &isolation);

      const IntentTypePair intent_types = GetWriteIntentsForIsolationLevel(isolation);

      for (const auto& doc_path : doc_paths) {
        current_intent_prefix.Clear();
        current_intent_prefix.AppendValueType(ValueType::kIntentPrefix);
        current_intent_prefix.AppendRawBytes(doc_path.encoded_doc_key().data());
        for (int i = 0; i < doc_path.num_subkeys(); i++) {
          RETURN_NOT_OK(resolver->ReadIntentConflicts(intent_types.weak, &current_intent_prefix));
          doc_path.subkey(i).AppendToKey(&current_intent_prefix);
        }
        RETURN_NOT_OK(resolver->ReadIntentConflicts(intent_types.strong, &current_intent_prefix));
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

Status ResolveTransactionConflicts(const KeyValueWriteBatchPB& write_batch,
                                   HybridTime hybrid_time,
                                   rocksdb::DB* db,
                                   TransactionStatusManager* status_manager) {
  DCHECK(hybrid_time.is_valid());
  TransactionConflictResolverContext context(write_batch, hybrid_time);
  ConflictResolver resolver(db, status_manager, &context);
  return resolver.Resolve();
}

Result<HybridTime> ResolveOperationConflicts(const DocOperations& doc_ops,
                                             HybridTime hybrid_time,
                                             rocksdb::DB* db,
                                             TransactionStatusManager* status_manager) {
  OperationConflictResolverContext context(&doc_ops, hybrid_time);
  ConflictResolver resolver(db, status_manager, &context);
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
  // Intent is encoded as "Prefix + DocPath + IntentType + DocHybridTime".
  result.doc_path.consume_byte();
  RETURN_NOT_OK(DocHybridTime::CheckAndGetEncodedSize(result.doc_path, &doc_ht_size));
  // 3 comes from (ValueType::kIntentType, the actual intent type, ValueType::kHybridTime).
  INTENT_KEY_SCHECK(result.doc_path.size(), GE, doc_ht_size + 3, "key too short");
  result.doc_path.remove_suffix(doc_ht_size + 3);
  auto intent_type_and_doc_ht = result.doc_path.end();
  INTENT_KEY_SCHECK(intent_type_and_doc_ht[0], EQ, static_cast<uint8_t>(ValueType::kIntentType),
                    "intent type value type expected");
  result.type = static_cast<IntentType>(intent_type_and_doc_ht[1]);
  INTENT_KEY_SCHECK(intent_type_and_doc_ht[2], EQ, static_cast<uint8_t>(ValueType::kHybridTime),
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
                ToString(parsed->type),
                doc_ht.ToString());
}

} // namespace docdb
} // namespace yb
