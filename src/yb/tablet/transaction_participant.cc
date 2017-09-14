//
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
//

#include "yb/tablet/transaction_participant.h"

#include <mutex>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>

#include <boost/optional/optional.hpp>

#include <boost/uuid/uuid_io.hpp>

#include "yb/rocksdb/write_batch.h"

#include "yb/client/transaction_rpc.h"

#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/key_bytes.h"

#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/monotime.h"

using namespace std::placeholders;

namespace yb {
namespace tablet {

namespace {

class RunningTransaction {
 public:
  explicit RunningTransaction(const TransactionId& id, const TransactionMetadataPB& metadata)
      : id_(id), metadata_(metadata) {
  }

  const TransactionId& id() const {
    return id_;
  }

  const TransactionMetadataPB& metadata() const {
    return metadata_;
  }

  bool committed_locally() const {
    return committed_locally_;
  }

  void SetCommittedLocally() {
    committed_locally_ = true;
  }

  void RequestStatusAt(client::YBClient* client,
                       HybridTime time,
                       RequestTransactionStatusCallback callback,
                       std::unique_lock<std::mutex>* lock) const {
    if (last_known_status_hybrid_time_ != HybridTime::kMin) {
      auto transaction_status =
          GetStatusAt(time, last_known_status_hybrid_time_, last_known_status_);
      if (transaction_status) {
        lock->unlock();
        callback(*transaction_status);
      }
    }
    bool was_empty = status_waiters_.empty();
    status_waiters_.emplace_back(std::move(callback), time);
    if (!was_empty) {
      return;
    }
    lock->unlock();
    auto deadline = MonoTime::FineNow() + MonoDelta::FromSeconds(5); // TODO(dtxn)
    tserver::GetTransactionStatusRequestPB req;
    req.set_tablet_id(metadata_.status_tablet());
    req.set_transaction_id(id_.begin(), id_.size());
    client::GetTransactionStatus(
        deadline,
        nullptr /* tablet */,
        client,
        &req,
        std::bind(&RunningTransaction::StatusReceived, this, _1, _2, lock->mutex()));
  }

 private:
  static boost::optional<tserver::TransactionStatus> GetStatusAt(
      HybridTime time,
      HybridTime last_known_status_hybrid_time,
      tserver::TransactionStatus last_known_status) {
    switch (last_known_status) {
      case tserver::TransactionStatus::ABORTED:
        return tserver::TransactionStatus::ABORTED;
      case tserver::TransactionStatus::COMMITTED:
        // TODO(dtxn) clock skew
        return last_known_status_hybrid_time > time
            ? tserver::TransactionStatus::PENDING
            : tserver::TransactionStatus::COMMITTED;
      case tserver::TransactionStatus::PENDING:
        if (last_known_status_hybrid_time >= time) {
          return tserver::TransactionStatus::PENDING;
        }
        return boost::none;
      default:
        FATAL_INVALID_ENUM_VALUE(tserver::TransactionStatus, last_known_status);
    }
  }

  void StatusReceived(const Status& status,
                      const tserver::GetTransactionStatusResponsePB& response,
                      std::mutex* mutex) const {
    std::vector<std::pair<RequestTransactionStatusCallback, HybridTime>> status_waiters;
    HybridTime time;
    tserver::TransactionStatus transaction_status;
    {
      std::unique_lock<std::mutex> lock(*mutex);
      status_waiters_.swap(status_waiters);
      if (status.ok()) {
        auto time = response.has_status_hybrid_time()
            ? HybridTime(response.status_hybrid_time())
            : HybridTime::kMax;
        if (last_known_status_hybrid_time_ <= time) {
          last_known_status_hybrid_time_ = time;
          last_known_status_ = response.status();
        }
        time = last_known_status_hybrid_time_;
        transaction_status = last_known_status_;
      }
    }
    if (!status.ok()) {
      for (const auto& waiter : status_waiters) {
        waiter.first(status);
      }
    } else {
      for (const auto& waiter : status_waiters) {
        auto status_for_waiter = GetStatusAt(waiter.second, time, transaction_status);
        if (status_for_waiter) {
          waiter.first(*status_for_waiter);
        } else {
          waiter.first(STATUS_FORMAT(
              TryAgain,
              "Cannot determine transaction status at $0, last known: $1 at $2",
              waiter.second,
              transaction_status,
              time));
        }
      }
    }
  }

  TransactionId id_;
  TransactionMetadataPB metadata_;
  bool committed_locally_ = false;

  mutable tserver::TransactionStatus last_known_status_;
  mutable HybridTime last_known_status_hybrid_time_ = HybridTime::kMin;
  mutable std::vector<std::pair<RequestTransactionStatusCallback, HybridTime>> status_waiters_;
};

} // namespace

class TransactionParticipant::Impl {
 public:
  explicit Impl(TransactionParticipantContext* context)
      : context_(*context) {}

  // Adds new running transaction.
  void Add(const TransactionMetadataPB& data, rocksdb::WriteBatch *write_batch) {
    auto id = MakeTransactionIdFromBinaryRepresentation(data.transaction_id());
    if (!id.ok()) {
      LOG(DFATAL) << "Invalid transaction id: " << id.status().ToString();
      return;
    }
    bool store = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = transactions_.find(*id);
      if (it == transactions_.end()) {
        transactions_.emplace(*id, data);
        store = true;
      } else {
        DCHECK_EQ(it->metadata().ShortDebugString(), data.ShortDebugString());
      }
    }
    if (store) {
      // TODO(dtxn) Load value if it is not loaded.
      docdb::KeyBytes key;
      AppendTransactionKeyPrefix(*id, &key);
      auto value = data.SerializeAsString();
      write_batch->Put(key.data(), value);
    }
  }

  bool CommittedLocally(const TransactionId& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = transactions_.find(id);
    return it != transactions_.end() && it->committed_locally();
  }

  yb::IsolationLevel IsolationLevel(rocksdb::DB* db, const TransactionId& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = transactions_.find(id);
    if (it == transactions_.end()) {
      docdb::KeyBytes key;
      AppendTransactionKeyPrefix(id, &key);
      auto iter = docdb::CreateRocksDBIterator(db,
                                               docdb::BloomFilterMode::DONT_USE_BLOOM_FILTER,
                                               boost::none,
                                               rocksdb::kDefaultQueryId);
      iter->Seek(key.data());
      if (iter->Valid() && iter->key() == key.data()) {
        TransactionMetadataPB metadata;
        if (metadata.ParseFromArray(iter->value().cdata(), iter->value().size())) {
          it = transactions_.emplace(id, metadata).first;
        } else {
          LOG(DFATAL) << "Unable to parse stored metadata: " << iter->value().ToDebugHexString();
        }
      }
    }
    return it != transactions_.end() ? it->metadata().isolation()
                                     : yb::IsolationLevel::NON_TRANSACTIONAL;
  }

  void RequestStatusAt(const TransactionId& id,
                       HybridTime time,
                       RequestTransactionStatusCallback callback) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = transactions_.find(id);
    if (it == transactions_.end()) {
      lock.unlock();
      callback(STATUS_FORMAT(NotFound, "Unknown transaction: $1", id));
      return;
    }
    return it->RequestStatusAt(context_.client().get(), time, std::move(callback), &lock);
  }

  CHECKED_STATUS ProcessApply(const TransactionApplyData& data) {
    CHECK_OK(data.applier->ApplyIntents(data));

    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = transactions_.find(data.transaction_id);
      if (it == transactions_.end()) {
        // This situation is normal and could be caused by 2 scenarios:
        // 1) Write batch failed, but originator doesn't know that.
        // 2) Failed to notify status tablet that we applied transaction.
        LOG(WARNING) << "Apply of unknown transaction: " << data.transaction_id;
        return Status::OK();
      } else {
        transactions_.modify(it, [](RunningTransaction& transaction) {
          transaction.SetCommittedLocally();
        });
        // TODO(dtxn) cleanup
      }
    }

    if (data.mode == ProcessingMode::LEADER) {
      auto deadline = MonoTime::FineNow() + MonoDelta::FromSeconds(5); // TODO(dtxn)
      tserver::UpdateTransactionRequestPB req;
      req.set_tablet_id(data.status_tablet);
      auto& state = *req.mutable_state();
      state.set_transaction_id(data.transaction_id.begin(), data.transaction_id.size());
      state.set_status(tserver::TransactionStatus::APPLIED_IN_ONE_OF_INVOLVED_TABLETS);
      state.add_tablets(context_.tablet_id());
      UpdateTransaction(
          deadline,
          nullptr /* remote_tablet */,
          context_.client().get(),
          &req,
          [](const Status& status) {
            LOG_IF(WARNING, !status.ok()) << "Failed to send applied: " << status.ToString();
          });
    }
    return Status::OK();
  }

 private:
  typedef boost::multi_index_container<RunningTransaction,
      boost::multi_index::indexed_by <
          boost::multi_index::hashed_unique <
              boost::multi_index::const_mem_fun<RunningTransaction,
                                                const TransactionId&,
                                                &RunningTransaction::id>
          >
      >
  > Transactions;

  TransactionParticipantContext& context_;

  std::mutex mutex_;
  Transactions transactions_;
};

TransactionParticipant::TransactionParticipant(TransactionParticipantContext* context)
    : impl_(new Impl(context)) {
}

TransactionParticipant::~TransactionParticipant() {
}

void TransactionParticipant::Add(const TransactionMetadataPB& data,
                                 rocksdb::WriteBatch *write_batch) {
  impl_->Add(data, write_batch);
}

IsolationLevel TransactionParticipant::IsolationLevel(rocksdb::DB* db, const TransactionId& id) {
  return impl_->IsolationLevel(db, id);
}

bool TransactionParticipant::CommittedLocally(const TransactionId& id) {
  return impl_->CommittedLocally(id);
}

void TransactionParticipant::RequestStatusAt(const TransactionId& id,
                                             HybridTime time,
                                             RequestTransactionStatusCallback callback) {
  return impl_->RequestStatusAt(id, time, std::move(callback));
}

CHECKED_STATUS TransactionParticipant::ProcessApply(const TransactionApplyData& data) {
  return impl_->ProcessApply(data);
}

void AppendTransactionKeyPrefix(const TransactionId& transaction_id, docdb::KeyBytes* out) {
  out->AppendValueType(docdb::ValueType::kIntentPrefix);
  out->AppendValueType(docdb::ValueType::kTransactionId);
  out->AppendRawBytes(Slice(transaction_id.data, transaction_id.size()));
}

} // namespace tablet
} // namespace yb
