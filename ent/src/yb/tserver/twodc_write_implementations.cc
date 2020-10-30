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

#include <deque>

#include "yb/tserver/twodc_write_interface.h"
#include "yb/tserver/tserver.pb.h"

#include "yb/cdc/cdc_service.pb.h"

#include "yb/util/size_literals.h"
#include "yb/util/flag_tags.h"
#include "yb/util/flags.h"
#include "yb/util/locks.h"

#include "yb/common/hybrid_time.h"

DEFINE_int32(cdc_max_apply_batch_num_records, 1024, "Max CDC write request batch num records. If"
                                                    " set to 0, there is no max num records, which"
                                                    " means batches will be limited only by size.");
TAG_FLAG(cdc_max_apply_batch_num_records, runtime);

DEFINE_int32(cdc_max_apply_batch_size_bytes, 0, "Max CDC write request batch size in kb. If 0,"
                                                " default to consensus_max_batch_size_bytes.");
TAG_FLAG(cdc_max_apply_batch_size_bytes, runtime);

DEFINE_test_flag(bool, twodc_write_hybrid_time, false,
                 "Override external_hybrid_time with initialHybridTimeValue for testing.");

DECLARE_int32(consensus_max_batch_size_bytes);

namespace yb {

using namespace yb::size_literals;

namespace tserver {
namespace enterprise {


// The SequentialWriteImplementation strategy sends one record per WriteRequestPB, and waits for a
// a response from one rpc before sending out another. This implementation sends rpcs in order
// of opid. Note that a single write request can still contain a batch of multiple key value pairs,
// corresponding to all the changes in a record.
class SequentialWriteImplementation : public TwoDCWriteInterface {
 public:
  ~SequentialWriteImplementation() = default;

  void ProcessRecord(const std::string& tablet_id, const cdc::CDCRecordPB& record) override {
    auto write_request = std::make_unique<WriteRequestPB>();
    write_request->set_tablet_id(tablet_id);
    if (PREDICT_FALSE(FLAGS_TEST_twodc_write_hybrid_time)) {
      // Used only for testing external hybrid time.
      write_request->set_external_hybrid_time(yb::kInitialHybridTimeValue);
    } else {
      write_request->set_external_hybrid_time(record.time());
    }

    for (const auto& kv_pair : record.changes()) {
      auto* write_pair = write_request->mutable_write_batch()->add_write_pairs();
      write_pair->set_key(kv_pair.key());
      write_pair->set_value(kv_pair.value().binary_value());
    }

    records_.push_back(std::move(write_request));
  }

  std::unique_ptr <WriteRequestPB> GetNextWriteRequest() override {
    auto next_req = std::move(records_.front());
    records_.pop_front();
    return next_req;
  }

  bool HasMoreWrites() override {
    return records_.size() > 0;
  }

 private:
  std::deque <std::unique_ptr<WriteRequestPB>> records_;

};

// The BatchedWriteImplementation strategy batches together multiple records per WriteRequestPB.
// Max number of records in a request is cdc_max_apply_batch_num_records, and max size of a request
// is cdc_max_apply_batch_size_kb. Batches are not sent by opid order, since a GetChangesResponse
// can contain interleaved records to multiple tablets. Rather, we send batches to each tablet
// in order for that tablet, before moving on to the next tablet.
class BatchedWriteImplementation : public TwoDCWriteInterface {
  ~BatchedWriteImplementation() = default;

  void ProcessRecord(const std::string& tablet_id, const cdc::CDCRecordPB& record) override {
    WriteRequestPB* write_request;
    std::lock_guard<decltype(lock_)> l(lock_);
    auto it = records_.find(tablet_id);
    if (it == records_.end()) {
      std::deque<std::unique_ptr<WriteRequestPB>> queue;
      records_.emplace(tablet_id, std::move(queue));
    }

    auto& queue = records_.at(tablet_id);

    auto max_batch_records = FLAGS_cdc_max_apply_batch_num_records != 0 ?
        FLAGS_cdc_max_apply_batch_num_records : std::numeric_limits<uint32_t>::max();
    auto max_batch_size = FLAGS_cdc_max_apply_batch_size_bytes != 0 ?
        FLAGS_cdc_max_apply_batch_size_bytes : FLAGS_consensus_max_batch_size_bytes;

    if (queue.empty() ||
        queue.back()->write_batch().write_pairs_size() >= max_batch_records ||
        queue.back()->ByteSize() >= max_batch_size) {
      // Create a new batch.
      auto req = std::make_unique<WriteRequestPB>();
      req->set_tablet_id(tablet_id);
      req->set_external_hybrid_time(record.time());
      queue.push_back(std::move(req));
    }
    write_request = queue.back().get();

    for (const auto& kv_pair : record.changes()) {
      auto* write_pair = write_request->mutable_write_batch()->add_write_pairs();
      write_pair->set_key(kv_pair.key());
      write_pair->set_value(kv_pair.value().binary_value());
      if (PREDICT_FALSE(FLAGS_TEST_twodc_write_hybrid_time)) {
        // Used only for testing external hybrid time.
        write_pair->set_external_hybrid_time(yb::kInitialHybridTimeValue);
      } else {
        write_pair->set_external_hybrid_time(record.time());
      }
    }
  }

  std::unique_ptr <WriteRequestPB> GetNextWriteRequest() override {
    std::lock_guard<decltype(lock_)> l(lock_);
    auto& queue = records_.begin()->second;
    auto next_req = std::move(queue.front());
    queue.pop_front();
    if (queue.size() == 0) {
      records_.erase(next_req->tablet_id());
    }
    return next_req;
  }

  bool HasMoreWrites() override {
    std::lock_guard<decltype(lock_)> l(lock_);
    return records_.size() > 0;
  }

 private:
  std::map <std::string, std::deque<std::unique_ptr < WriteRequestPB>>>
  records_;
  mutable rw_spinlock lock_;
};

void ResetWriteInterface(std::unique_ptr<TwoDCWriteInterface>* write_strategy) {
  if (FLAGS_cdc_max_apply_batch_num_records == 1) {
    write_strategy->reset(new SequentialWriteImplementation());
  } else {
    write_strategy->reset(new BatchedWriteImplementation());
  }
}

} // namespace enterprise
} // namespace tserver
} // namespace yb
