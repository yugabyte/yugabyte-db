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

#include "yb/master/state_with_tablets.h"

#include "yb/util/enums.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

DEFINE_test_flag(bool, mark_snasphot_as_failed, false,
                 "Whether we should skip sending RESTORE_FINISHED to tablets.");

namespace yb {
namespace master {

namespace {

const std::initializer_list<std::pair<SysSnapshotEntryPB::State, SysSnapshotEntryPB::State>>
    kStateTransitions = {
  { SysSnapshotEntryPB::CREATING, SysSnapshotEntryPB::COMPLETE },
  { SysSnapshotEntryPB::DELETING, SysSnapshotEntryPB::DELETED },
  { SysSnapshotEntryPB::RESTORING, SysSnapshotEntryPB::RESTORED },
};

SysSnapshotEntryPB::State InitialStateToTerminalState(SysSnapshotEntryPB::State state) {
  for (const auto& initial_and_terminal_states : kStateTransitions) {
    if (state == initial_and_terminal_states.first) {
      if (PREDICT_FALSE(FLAGS_TEST_mark_snasphot_as_failed)
          && state == SysSnapshotEntryPB::RESTORING) {
        LOG(INFO) << "TEST: Mark COMPETE snapshot as FAILED";
        return SysSnapshotEntryPB::FAILED;
      }
      return initial_and_terminal_states.second;
    }
  }

  FATAL_INVALID_PB_ENUM_VALUE(SysSnapshotEntryPB::State, state);
}

} // namespace

StateWithTablets::StateWithTablets(
    SnapshotCoordinatorContext* context, SysSnapshotEntryPB::State initial_state,
    std::string log_prefix)
    : initial_state_(initial_state), context_(*context), log_prefix_(std::move(log_prefix)) {
}

Result<SysSnapshotEntryPB::State> StateWithTablets::AggregatedState() const {
  if (tablets_.empty()) {
    return InitialStateToTerminalState(initial_state_);
  }
  SysSnapshotEntryPB::State result = initial_state_;
  bool has_initial = false;
  for (const auto& tablet : tablets_) {
    if (tablet.state == SysSnapshotEntryPB::FAILED) {
      return SysSnapshotEntryPB::FAILED;
    } else if (tablet.state == initial_state_) {
      has_initial = true;
    } else if (result == initial_state_) {
      result = tablet.state;
    } else if (tablet.state != result) {
      // Should not happen.
      return STATUS_FORMAT(IllegalState, "Tablets in different terminal states: $0 and $1",
                           SysSnapshotEntryPB::State_Name(result),
                           SysSnapshotEntryPB::State_Name(tablet.state));
    }
  }
  return has_initial ? initial_state_ : result;
}

Status StateWithTablets::AnyFailure() const {
  for (const auto& tablet : tablets_) {
    if (tablet.state == SysSnapshotEntryPB::FAILED) {
      return tablet.last_error;
    }
  }
  return Status::OK();
}

bool StateWithTablets::AllTabletsDone() const {
  return num_tablets_in_initial_state_ == 0;
}

bool StateWithTablets::PassedSinceCompletion(const MonoDelta& duration) const {
  if (!AllTabletsDone()) {
    return false;
  }

  if (complete_at_ == CoarseTimePoint()) {
    YB_LOG_EVERY_N_SECS(DFATAL, 30)
        << LogPrefix() << "All tablets done but complete done was not set";
    return false;
  }

  return CoarseMonoClock::Now() > complete_at_ + duration;
}

std::vector<TabletId> StateWithTablets::TabletIdsInState(SysSnapshotEntryPB::State state) {
  std::vector<TabletId> result;
  result.reserve(tablets_.size());
  for (const auto& tablet : tablets_) {
    if (tablet.state == state) {
      result.push_back(tablet.id);
    }
  }
  return result;
}


void StateWithTablets::Done(const TabletId& tablet_id, Status status) {
  VLOG_WITH_PREFIX_AND_FUNC(4) << tablet_id << ", " << status;

  auto it = tablets_.find(tablet_id);
  if (it == tablets_.end()) {
    LOG_WITH_PREFIX(DFATAL)
        << "Finished " << InitialStateName() <<  " at unknown tablet "
        << tablet_id << ": " << status;
    return;
  }

  if (!it->running) {
    LOG_WITH_PREFIX(DFATAL)
        << "Finished " << InitialStateName() <<  " at " << tablet_id
        << " that is not running and in state " << SysSnapshotEntryPB::State_Name(it->state)
        << ": " << status;
    return;
  }

  tablets_.modify(it, [](TabletData& data) { data.running = false; });

  if (it->aborted) {
    LOG_WITH_PREFIX(INFO) << Format("Tablet $0 was aborted before task finished.", tablet_id);
    DecrementTablets();
    return;
  }

  const auto& state = it->state;
  if (state == initial_state_) {
    status = CheckDoneStatus(status);
    if (status.ok()) {
      tablets_.modify(
          it, [terminal_state = InitialStateToTerminalState(initial_state_)](TabletData& data) {
            data.state = terminal_state;
          });
      LOG_WITH_PREFIX(INFO) << "Finished " << InitialStateName() << " at " << tablet_id << ", "
                            << num_tablets_in_initial_state_ << " was running";
    } else {
      auto full_status = status.CloneAndPrepend(
          Format("Failed to $0 snapshot at $1", InitialStateName(), tablet_id));
      auto maybe_terminal_state = GetTerminalStateForStatus(status);
      tablets_.modify(it, [&full_status, maybe_terminal_state](TabletData& data) {
        if (maybe_terminal_state) {
          data.state = maybe_terminal_state.value();
        }
        data.last_error = full_status;
      });

      LOG_WITH_PREFIX(WARNING) << Format(
          "$0, terminal: $1, $2 was running", full_status, maybe_terminal_state.has_value(),
          num_tablets_in_initial_state_);
      if (!maybe_terminal_state) {
        return;
      }
    }
    DecrementTablets();
  } else {
    LOG_WITH_PREFIX(DFATAL)
        << "Finished " << InitialStateName() << " at tablet " << tablet_id << " in a wrong state "
        << state << ": " << status;
  }
}

bool StateWithTablets::AllInState(SysSnapshotEntryPB::State state) {
  for (const auto& tablet : tablets_) {
    if (tablet.state != state) {
      return false;
    }
  }

  return true;
}

bool StateWithTablets::HasInState(SysSnapshotEntryPB::State state) {
  for (const auto& tablet : tablets_) {
    if (tablet.state == state) {
      return true;
    }
  }

  return false;
}

void StateWithTablets::SetInitialTabletsState(SysSnapshotEntryPB::State state) {
  initial_state_ = state;
  for (auto it = tablets_.begin(); it != tablets_.end(); ++it) {
    tablets_.modify(it, [state](TabletData& data) {
      data.state = state;
    });
  }
  num_tablets_in_initial_state_ = tablets_.size();
}

const std::string& StateWithTablets::InitialStateName() const {
  return SysSnapshotEntryPB::State_Name(initial_state_);
}

void StateWithTablets::CheckCompleteness() {
  if (num_tablets_in_initial_state_ == 0) {
    complete_at_ = CoarseMonoClock::Now();
  }
}

void StateWithTablets::RemoveTablets(const std::vector<std::string>& tablet_ids) {
  for (const auto& id : tablet_ids) {
    tablets_.erase(id);
  }
}

const std::string& StateWithTablets::LogPrefix() const {
  return log_prefix_;
}

} // namespace master
} // namespace yb
