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

#include "yb/docdb/docdb_compaction_filter_intents.h"

#include <memory>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "yb/rocksdb/compaction_filter.h"
#include "yb/util/string_util.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/intent.h"
#include "yb/docdb/value.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/rpc/rpc.h"
#include "yb/rpc/thread_pool.h"
#include "yb/tablet/transaction_participant.h"
#include "yb/util/flag_tags.h"

DEFINE_uint64(aborted_intent_cleanup_ms, 60000, // 1 minute by default, 1 sec for testing
             "Duration in ms after which to check if a transaction is aborted.");

DEFINE_int32(aborted_intent_cleanup_max_batch_size, 1000, // Cleanup 10000 transactions at a time
             "Number of transactions to collect for possible cleanup.");

using std::shared_ptr;
using std::unique_ptr;
using std::unordered_set;
using rocksdb::CompactionFilter;
using rocksdb::VectorToString;

namespace yb {
namespace docdb {

// ------------------------------------------------------------------------------------------------

DocDBIntentsCompactionFilter::DocDBIntentsCompactionFilter(tablet::Tablet* tablet)
    : tablet_(tablet) {}

DocDBIntentsCompactionFilter::~DocDBIntentsCompactionFilter() {
  VLOG(3) << "DocDB intents compaction filter is being deleted";
  if (transactions_to_cleanup_.empty()) {
    return;
  }
  TransactionStatusManager* manager = tablet_->transaction_participant();
  if (rejected_transactions_ > 0) {
    LOG(WARNING) << "Number of aborted transactions not cleaned up " <<
                 "on account of reaching size limits:" << rejected_transactions_;
  }
  manager->Cleanup(std::move(transactions_to_cleanup_));
}

bool DocDBIntentsCompactionFilter::DoFilter(int level, const rocksdb::Slice& key,
                                            const rocksdb::Slice& existing_value,
                                            std::string* new_value, bool* value_changed) {
  if (!filter_usage_logged_) {
    VLOG(3) << "DocDB intents compaction filter is being used for a compaction";
    filter_usage_logged_ = true;
  }

  // Find transaction metadata row.
  if (GetKeyType(key, StorageDbType::kIntents) == KeyType::kTransactionMetadata) {
    TransactionMetadataPB metadata_pb;
    if (!metadata_pb.ParseFromArray(existing_value.cdata(), existing_value.size())) {
      LOG(ERROR) << "Transaction metadata failed to parse.";
      return false;
    }
    Result<TransactionMetadata> metadata = TransactionMetadata::FromPB(metadata_pb);
    if (!metadata.ok()) {
      LOG(ERROR) << "Invalid Transaction metadata parse status:" << metadata.status();
      return false;
    }
    auto result = DecodeTransactionIdFromIntentValue(const_cast<Slice*>(&key));
    if (!result.ok()) {
      LOG(ERROR) << "Could not decode Transaction metadata: " << result.status();
      return false;
    }
    HybridTime start_time = metadata->start_time;
    if (GetCurrentTimeMicros() - start_time.GetPhysicalValueMicros() >
        FLAGS_aborted_intent_cleanup_ms) {
      AddToSet(*result);
    }
  }
  return false;
}

void DocDBIntentsCompactionFilter::AddToSet(TransactionId transactionId) {
  if (transactions_to_cleanup_.size() <= FLAGS_aborted_intent_cleanup_max_batch_size) {
    transactions_to_cleanup_.insert(transactionId);
  } else {
    rejected_transactions_++;
  }
}

const char* DocDBIntentsCompactionFilter::Name() const {
  return "DocDBIntentsCompactionFilter";
}

// ------------------------------------------------------------------------------------------------

DocDBIntentsCompactionFilterFactory::DocDBIntentsCompactionFilterFactory(tablet::Tablet* tablet)
    : tablet_(tablet) {}

DocDBIntentsCompactionFilterFactory::~DocDBIntentsCompactionFilterFactory() {}

unique_ptr<CompactionFilter> DocDBIntentsCompactionFilterFactory::CreateCompactionFilter(
    const CompactionFilter::Context& context) {
  return std::make_unique<DocDBIntentsCompactionFilter>(tablet_);
}

const char* DocDBIntentsCompactionFilterFactory::Name() const {
  return "DocDBIntentsCompactionFilterFactory";
}

}  // namespace docdb
}  // namespace yb
