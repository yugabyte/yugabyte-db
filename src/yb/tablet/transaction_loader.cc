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

#include "yb/tablet/transaction_loader.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/intent.h"

#include "yb/docdb/bounded_rocksdb_iterator.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/iter_util.h"

#include "yb/tablet/transaction_status_resolver.h"

#include "yb/util/bitmap.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/operation_counter.h"
#include "yb/util/pb_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/thread.h"

using namespace std::literals;

DEFINE_test_flag(int32, inject_load_transaction_delay_ms, 0,
                 "Inject delay before loading each transaction at startup.");

DECLARE_bool(TEST_fail_on_replicated_batch_idx_set_in_txn_record);

METRIC_DEFINE_simple_counter(
    tablet, transaction_load_attempts,
    "Total number of attempts to load transaction metadata from the intents RocksDB",
    yb::MetricUnit::kTransactions);

namespace yb {
namespace tablet {

namespace {

docdb::BoundedRocksDbIterator CreateFullScanIterator(rocksdb::DB* db) {
  return docdb::BoundedRocksDbIterator(docdb::CreateRocksDBIterator(
      db, &docdb::KeyBounds::kNoBounds,
      docdb::BloomFilterMode::DONT_USE_BLOOM_FILTER,
      /* user_key_for_filter= */ boost::none, rocksdb::kDefaultQueryId));
}

} // namespace

class TransactionLoader::Executor {
 public:
  explicit Executor(
      TransactionLoader* loader,
      RWOperationCounter* pending_op_counter_blocking_rocksdb_shutdown_start)
      : loader_(*loader),
        scoped_pending_operation_(pending_op_counter_blocking_rocksdb_shutdown_start) {
    metric_transaction_load_attempts_ =
        METRIC_transaction_load_attempts.Instantiate(loader_.entity_);
  }

  bool Start(const docdb::DocDB& db) {
    if (!scoped_pending_operation_.ok()) {
      return false;
    }
    regular_iterator_ = CreateFullScanIterator(db.regular);
    intents_iterator_ = CreateFullScanIterator(db.intents);
    CHECK_OK(yb::Thread::Create(
        "transaction_loader", "loader", &Executor::Execute, this, &loader_.load_thread_))
    return true;
  }

 private:
  void Execute() {
    SetThreadName("TransactionLoader");

    Status status;

    auto se = ScopeExit([this, &status] {
      loader_.FinishLoad(status);
      // Destroy this executor object. Must be the last statement before we return from the Execute
      // function.
      loader_.executor_.reset();
    });

    LOG_WITH_PREFIX(INFO) << "Load transactions start";

    status = LoadPendingApplies();
    if (!status.ok()) {
      return;
    }
    status = LoadTransactions();
  }

  Status CheckForShutdown() {
    if (loader_.shutdown_requested_.load(std::memory_order_acquire)) {
      return STATUS(IllegalState, "Shutting down");
    }
    return Status::OK();
  }

  Status LoadTransactions() EXCLUDES(loader_.pending_applies_mtx_) {
    size_t loaded_transactions = 0;
    TransactionId id = TransactionId::Nil();
    docdb::AppendTransactionKeyPrefix(id, &current_key_);
    intents_iterator_.Seek(current_key_.AsSlice());
    while (intents_iterator_.Valid()) {
      RETURN_NOT_OK(CheckForShutdown());
      auto key = intents_iterator_.key();
      if (!key.TryConsumeByte(dockv::KeyEntryTypeAsChar::kTransactionId)) {
        break;
      }
      auto decode_id_result = DecodeTransactionId(&key);
      if (!decode_id_result.ok()) {
        LOG_WITH_PREFIX(DFATAL)
            << "Failed to decode transaction id from: " << key.ToDebugHexString();
        intents_iterator_.Next();
        continue;
      }
      id = *decode_id_result;
      current_key_.Clear();
      docdb::AppendTransactionKeyPrefix(id, &current_key_);
      if (key.empty()) { // The key only contains a transaction id - it is metadata record.
        if (FLAGS_TEST_inject_load_transaction_delay_ms > 0) {
          std::this_thread::sleep_for(FLAGS_TEST_inject_load_transaction_delay_ms * 1ms);
        }
        RETURN_NOT_OK(LoadTransaction(id));
        ++loaded_transactions;
      }
      current_key_.AppendKeyEntryType(dockv::KeyEntryType::kMaxByte);
      intents_iterator_.Seek(current_key_.AsSlice());
    }
    RETURN_NOT_OK(intents_iterator_.status());

    intents_iterator_.Reset();

    RETURN_NOT_OK(CheckForShutdown());
    context().CompleteLoad([this] {
      loader_.state_ = TransactionLoaderState::kLoadCompleted;
    });
    {
      // We need to lock and unlock the mutex here to avoid missing a notification in WaitLoaded
      // and WaitAllLoaded. The waiting loop in those functions is equivalent to the following,
      // after locking the mutex (and of course wait(...) releases the mutex while waiting):
      //
      // 1 while (!all_loaded_) {
      // 2   load_cond_.wait(lock);
      // 3 }
      //
      // If we did not have the lock/unlock here, it would be possible that all_loaded_ would be set
      // to true and notify_all() would be called between lines 1 and 2, and we would miss the
      // notification and wait indefinitely at line 2. With lock/unlock this is no longer possible
      // because if we set all_loaded_ to true between lines 1 and 2, the next opportunity for this
      // thread to send a notification would be at line 2 after wait(...) releases the mutex, but
      // after that we would check all_loaded_ and exit the loop at line 1.
      std::lock_guard lock(loader_.mutex_);
    }
    loader_.load_cond_.notify_all();
    LOG_WITH_PREFIX(INFO) << __func__ << " done: loaded " << loaded_transactions << " transactions";
    return Status::OK();
  }

  Status LoadPendingApplies() EXCLUDES(loader_.pending_applies_mtx_) {
    std::lock_guard lock(loader_.pending_applies_mtx_);

    std::array<char, 1 + sizeof(TransactionId) + 1> seek_buffer;
    seek_buffer[0] = dockv::KeyEntryTypeAsChar::kTransactionApplyState;
    seek_buffer[seek_buffer.size() - 1] = dockv::KeyEntryTypeAsChar::kMaxByte;
    regular_iterator_.Seek(Slice(seek_buffer.data(), 1));

    while (regular_iterator_.Valid()) {
      RETURN_NOT_OK(CheckForShutdown());
      auto key = regular_iterator_.key();
      if (!key.TryConsumeByte(dockv::KeyEntryTypeAsChar::kTransactionApplyState)) {
        break;
      }
      auto txn_id = DecodeTransactionId(&key);
      if (!txn_id.ok() || !key.TryConsumeByte(dockv::KeyEntryTypeAsChar::kGroupEnd)) {
        LOG_WITH_PREFIX(DFATAL) << "Wrong txn id: " << regular_iterator_.key().ToDebugString();
        regular_iterator_.Next();
        continue;
      }
      Slice value = regular_iterator_.value();
      if (value.TryConsumeByte(dockv::ValueEntryTypeAsChar::kString)) {
        auto pb = pb_util::ParseFromSlice<docdb::ApplyTransactionStatePB>(value);
        if (!pb.ok()) {
          LOG_WITH_PREFIX(DFATAL) << "Failed to decode apply state pb from RocksDB"
                                  << key.ToDebugString() << ": " << pb.status();
          regular_iterator_.Next();
          continue;
        }

        auto state = docdb::ApplyTransactionState::FromPB(*pb);
        if (!state.ok()) {
          LOG_WITH_PREFIX(DFATAL) << "Failed to decode apply state from stored pb "
              << state.status();
          regular_iterator_.Next();
          continue;
        }

        auto it = loader_.pending_applies_.emplace(*txn_id, ApplyStateWithCommitHt {
          .state = state.get(),
          .commit_ht = HybridTime(pb->commit_ht())
        }).first;

        VLOG_WITH_PREFIX(4) << "Loaded pending apply for " << *txn_id << ": "
                            << it->second.ToString();
      } else if (value.TryConsumeByte(dockv::ValueEntryTypeAsChar::kTombstone)) {
        VLOG_WITH_PREFIX(4) << "Found deleted large apply for " << *txn_id;
      } else {
        LOG_WITH_PREFIX(DFATAL)
            << "Unexpected value type in apply state: " << value.ToDebugString();
      }

      memcpy(seek_buffer.data() + 1, txn_id->data(), txn_id->size());
      ROCKSDB_SEEK(&regular_iterator_, Slice(seek_buffer));
    }
    return regular_iterator_.status();
  }

  // id - transaction id to load.
  Status LoadTransaction(const TransactionId& id) EXCLUDES(loader_.pending_applies_mtx_) {
    metric_transaction_load_attempts_->Increment();
    VLOG_WITH_PREFIX(1) << "Loading transaction: " << id;

    TransactionMetadataPB metadata_pb;

    const Slice& value = intents_iterator_.value();
    if (!metadata_pb.ParseFromArray(value.cdata(), narrow_cast<int>(value.size()))) {
      return STATUS_FORMAT(
          IllegalState, "Unable to parse stored metadata: $0", value.ToDebugHexString());
    }

    auto metadata = TransactionMetadata::FromPB(metadata_pb);
    RETURN_NOT_OK_PREPEND(metadata, "Loaded bad metadata: ");

    if (!metadata->start_time.is_valid()) {
      metadata->start_time = HybridTime::kMin;
      LOG_WITH_PREFIX(INFO) << "Patched start time " << metadata->transaction_id << ": "
                            << metadata->start_time;
    }

    TransactionalBatchData last_batch_data;
    OneWayBitmap replicated_batches;
    RETURN_NOT_OK(FetchLastBatchData(id, &last_batch_data, &replicated_batches));

    if (!status_resolver_) {
      status_resolver_ = &context().AddStatusResolver();
    }
    status_resolver_->Add(metadata->status_tablet, id);

    auto pending_apply = loader_.GetPendingApply(id);
    context().LoadTransaction(
        std::move(*metadata), std::move(last_batch_data), std::move(replicated_batches),
        pending_apply ? &*pending_apply : nullptr);
    {
      std::lock_guard lock(loader_.mutex_);
      loader_.last_loaded_ = id;
    }
    loader_.load_cond_.notify_all();
    return Status::OK();
  }

  Status FetchLastBatchData(
      const TransactionId& id,
      TransactionalBatchData* last_batch_data,
      OneWayBitmap* replicated_batches) {
    current_key_.AppendKeyEntryType(dockv::KeyEntryType::kMaxByte);
    intents_iterator_.Seek(current_key_.AsSlice());
    if (intents_iterator_.Valid()) {
      intents_iterator_.Prev();
    } else {
      RETURN_NOT_OK(intents_iterator_.status());
      intents_iterator_.SeekToLast();
    }
    current_key_.RemoveLastByte();
    // Fetch the last batch of the current transaction having a strong intent by backward scan of
    // relevant portion in the reverse index section. During the backward scan, we break after
    // processing the first encountered strong intent.
    //
    // Note: We explicitly check if the transaction id is a strict prefix of the intent key so as
    // not process the transaction meta record and instead terminate the loop. Else, we would error
    // while processing transactions that executed statements of type 'FOR KEY SHARE' alone, since
    // such statements don't write strong intents.
    while (intents_iterator_.Valid() && intents_iterator_.key().size() > current_key_.size() &&
           intents_iterator_.key().starts_with(current_key_)) {
      auto decoded_key = dockv::DecodeIntentKey(intents_iterator_.value());
      LOG_IF_WITH_PREFIX(DFATAL, !decoded_key.ok())
          << "Failed to decode intent while loading transaction " << id << ", "
          << intents_iterator_.key().ToDebugHexString() << " => "
          << intents_iterator_.value().ToDebugHexString() << ": " << decoded_key.status();
      if (decoded_key.ok() && dockv::HasStrong(decoded_key->intent_types)) {
        last_batch_data->hybrid_time = CHECK_RESULT(decoded_key->doc_ht.Decode()).hybrid_time();
        Slice rev_key_slice(intents_iterator_.value());
        // Required by the transaction sealing protocol.
        if (!rev_key_slice.empty() && rev_key_slice[0] == dockv::KeyEntryTypeAsChar::kBitSet) {
          CHECK(!FLAGS_TEST_fail_on_replicated_batch_idx_set_in_txn_record);
          rev_key_slice.remove_prefix(1);
          auto result = OneWayBitmap::Decode(&rev_key_slice);
          if (result.ok()) {
            *replicated_batches = std::move(*result);
            VLOG_WITH_PREFIX(1) << "Decoded replicated batches for " << id << ": "
                                << replicated_batches->ToString();
          } else {
            LOG_WITH_PREFIX(DFATAL)
                << "Failed to decode replicated batches from "
                << intents_iterator_.value().ToDebugHexString() << ": " << result.status();
          }
        }
        std::string rev_key = rev_key_slice.ToBuffer();
        intents_iterator_.Seek(rev_key);
        // Delete could run in parallel to this load, and since our deletes break snapshot read
        // we could get into a situation when metadata and reverse record were successfully read,
        // but intent record could not be found.
        if (intents_iterator_.Valid() && intents_iterator_.key().starts_with(rev_key)) {
          VLOG_WITH_PREFIX(1)
              << "Found latest record for " << id
              << ": " << dockv::SubDocKey::DebugSliceToString(intents_iterator_.key())
              << " => " << intents_iterator_.value().ToDebugHexString();
          auto txn_id_slice = id.AsSlice();
          auto decoded_value_or_status = dockv::DecodeIntentValue(
              intents_iterator_.value(), &txn_id_slice);
          LOG_IF_WITH_PREFIX(DFATAL, !decoded_value_or_status.ok())
              << "Failed to decode intent value: " << decoded_value_or_status.status() << ", "
              << dockv::SubDocKey::DebugSliceToString(intents_iterator_.key()) << " => "
              << intents_iterator_.value().ToDebugHexString();
          if (decoded_value_or_status.ok()) {
            last_batch_data->next_write_id = decoded_value_or_status->write_id;
          }
          ++last_batch_data->next_write_id;
        }
        break;
      }
      RETURN_NOT_OK(intents_iterator_.status());
      intents_iterator_.Prev();
    }
    return intents_iterator_.status();
  }

  TransactionLoaderContext& context() const {
    return loader_.context_;
  }

  const std::string& LogPrefix() const {
    return context().LogPrefix();
  }

  TransactionLoader& loader_;
  ScopedRWOperation scoped_pending_operation_;

  docdb::BoundedRocksDbIterator regular_iterator_;
  docdb::BoundedRocksDbIterator intents_iterator_;

  // Buffer that contains key of current record, i.e. value type + transaction id.
  dockv::KeyBytes current_key_;

  TransactionStatusResolver* status_resolver_ = nullptr;

  scoped_refptr<Counter> metric_transaction_load_attempts_;
};

TransactionLoader::TransactionLoader(
    TransactionLoaderContext* context, const scoped_refptr<MetricEntity>& entity)
    : context_(*context), entity_(entity) {}

TransactionLoader::~TransactionLoader() {
}

void TransactionLoader::Start(
    RWOperationCounter* pending_op_counter_blocking_rocksdb_shutdown_start,
    const docdb::DocDB& db) {
  executor_ = std::make_unique<Executor>(this, pending_op_counter_blocking_rocksdb_shutdown_start);
  if (!executor_->Start(db)) {
    executor_ = nullptr;
  }
}

namespace {

// Waiting threads will only wake up on a timeout if there is still an uncaught race condition that
// causes us to miss a notification on the condition variable.
constexpr auto kWaitLoadedWakeUpInterval = 10s;

}  // namespace

Status TransactionLoader::WaitLoaded(const TransactionId& id) NO_THREAD_SAFETY_ANALYSIS {
  if (state_.load(std::memory_order_acquire) == TransactionLoaderState::kLoadCompleted) {
    return Status::OK();
  }
  std::unique_lock<std::mutex> lock(mutex_);
  // Defensively wake up at least once a second to avoid deadlock due to any issue similar to #8696.
  while (state_.load(std::memory_order_acquire) == TransactionLoaderState::kLoadNotFinished) {
    if (last_loaded_ >= id) {
      break;
    }
    load_cond_.wait_for(lock, kWaitLoadedWakeUpInterval);
  }
  return load_status_;
}

// Disable thread safety analysis because std::unique_lock is used.
Status TransactionLoader::WaitAllLoaded() NO_THREAD_SAFETY_ANALYSIS {
  // Read state_ with sequential consistency to prevent subtle bugs with operation reordering.
  // WaitAllLoaded is only invoked when opening a tablet.
  if (state_ == TransactionLoaderState::kLoadCompleted) {
    return Status::OK();
  }
  // Defensively wake up at least once a second to avoid deadlock due to any issue similar to #8696.
  std::unique_lock<std::mutex> lock(mutex_);
  while (state_ == TransactionLoaderState::kLoadNotFinished) {
    load_cond_.wait_for(lock, kWaitLoadedWakeUpInterval);
  }
  return load_status_;
}

std::optional<ApplyStateWithCommitHt> TransactionLoader::GetPendingApply(
    const TransactionId& id) const {
  if (pending_applies_removed_.load(std::memory_order_acquire)) {
    return std::nullopt;
  }
  std::lock_guard lock(pending_applies_mtx_);
  auto it = pending_applies_.find(id);
  if (it == pending_applies_.end()) {
    return std::nullopt;
  }
  return it->second;
}

void TransactionLoader::StartShutdown() {
  std::lock_guard lock(mutex_);
  shutdown_requested_ = true;
}

void TransactionLoader::CompleteShutdown() {
  if (load_thread_) {
    load_thread_->Join();
  }
}

void TransactionLoader::FinishLoad(Status status) {
  if (status.ok()) {
    context_.LoadFinished();
    return;
  }

  std::lock_guard lock(mutex_);
  load_status_ = status;
  state_.store(TransactionLoaderState::kLoadFailed, std::memory_order_release);
}

ApplyStatesMap TransactionLoader::MovePendingApplies() {
  std::lock_guard lock(pending_applies_mtx_);
  pending_applies_removed_ = true;
  return std::move(pending_applies_);
}

} // namespace tablet
} // namespace yb
