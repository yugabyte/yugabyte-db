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

#include "yb/tserver/local_catalog_service.h"

#include <utility>

#include "yb/rpc/rpc_context.h"

#include "yb/util/flags.h"
#include "yb/util/scope_exit.h"
#include "yb/util/monotime.h"

DEFINE_test_flag(bool, local_catalog_record_pushes, false,
    "Record the master's system catalog pushes instead of answering that this tserver keeps no "
    "copy of the catalog. A batch is recorded only if it starts at the receiver's position, which "
    "the previous recorded batch ended at.");

DEFINE_test_flag(bool, local_catalog_fail_pushes, false,
    "Fail the master's system catalog pushes.");

DEFINE_test_flag(bool, local_catalog_answer_gap, false,
    "Answer the master's recorded system catalog pushes with a gap at the receiver's position, "
    "applying nothing.");

DEFINE_test_flag(bool, local_catalog_answer_gap_behind, false,
    "Answer the master's recorded system catalog pushes with a gap one entry before the "
    "receiver's position, moving the position back, and applying nothing.");

DEFINE_test_flag(int32, local_catalog_push_answer_delay_ms, 0,
    "Delay the answer to each of the master's system catalog pushes by this many milliseconds.");

DEFINE_test_flag(int32, local_catalog_push_reseed_answer_delay_ms, 0,
    "Delay the answer to each of the master's system catalog reseeds by this many milliseconds.");

namespace yb::tserver {

LocalCatalogServiceImpl::LocalCatalogServiceImpl(const scoped_refptr<MetricEntity>& metric_entity)
    : LocalCatalogServiceIf(metric_entity) {}

void LocalCatalogServiceImpl::PushCatalogChanges(
    const PushCatalogChangesRequestPB* req, PushCatalogChangesResponsePB* resp,
    rpc::RpcContext context) {
  auto se = ScopeExit([this] { ++TEST_num_answered_; });
  if (FLAGS_TEST_local_catalog_push_answer_delay_ms > 0) {
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_local_catalog_push_answer_delay_ms));
  }
  if (req->reseed_required() && FLAGS_TEST_local_catalog_push_reseed_answer_delay_ms > 0) {
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_local_catalog_push_reseed_answer_delay_ms));
  }
  if (FLAGS_TEST_local_catalog_fail_pushes || TEST_fail_pushes_) {
    context.RespondFailure(STATUS(ServiceUnavailable, "Failing system catalog pushes for a test"));
    return;
  }
  if (!FLAGS_TEST_local_catalog_record_pushes) {
    resp->set_code(PushCatalogChangesResponsePB::NO_COPY);
    context.RespondSuccess();
    return;
  }

  const auto from_op_id = OpId::FromPB(req->from_op_id());
  const auto to_op_id = OpId::FromPB(req->to_op_id());
  {
    std::lock_guard lock(mutex_);
    if (FLAGS_TEST_local_catalog_answer_gap) {
      resp->set_code(PushCatalogChangesResponsePB::GAP);
    } else if (FLAGS_TEST_local_catalog_answer_gap_behind) {
      if (position_.index > 0) {
        --position_.index;
      }
      resp->set_code(PushCatalogChangesResponsePB::GAP);
    } else if (req->reseed_required()) {
      recorded_pushes_.push_back(*req);
      resp->set_code(PushCatalogChangesResponsePB::OK);
    } else if (from_op_id == position_) {
      recorded_pushes_.push_back(*req);
      position_ = to_op_id;
      if (req->has_safe_time()) {
        const HybridTime safe_time(req->safe_time());
        if (!safe_time_.is_valid() || safe_time > safe_time_) {
          safe_time_ = safe_time;
        }
      }
      resp->set_code(PushCatalogChangesResponsePB::OK);
    } else if (to_op_id.index <= position_.index) {
      resp->set_code(PushCatalogChangesResponsePB::OK);
    } else {
      resp->set_code(PushCatalogChangesResponsePB::GAP);
    }
    position_.ToPB(resp->mutable_applied_op_id());
    if (safe_time_.is_valid()) {
      resp->set_safe_time(safe_time_.ToUint64());
    }
  }
  context.RespondSuccess();
}

void LocalCatalogServiceImpl::TEST_SetPosition(const OpId& position) {
  std::lock_guard lock(mutex_);
  position_ = position;
  safe_time_ = HybridTime::kInvalid;
}

std::vector<PushCatalogChangesRequestPB> LocalCatalogServiceImpl::TEST_TakeRecordedPushes() {
  std::lock_guard lock(mutex_);
  return std::exchange(recorded_pushes_, {});
}

}  // namespace yb::tserver
