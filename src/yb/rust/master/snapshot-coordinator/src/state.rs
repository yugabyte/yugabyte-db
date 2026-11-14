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

//! The rules of `StateWithTablets`, `SnapshotState`, `RestorationState` and
//! `SnapshotScheduleState` as functions of a row and this term's tablet outcomes.
//!
//! A row keeps its *initial* state (CREATING, DELETING, RESTORING) and each tablet's state; the
//! state a client sees is aggregated from the tablets. Tablet outcomes this term has not written
//! yet are merged in, as C++ keeps them in memory until the object's tablets are all done.

use std::collections::HashMap;

use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::sys_snapshot_entry_pb::State;
use yb_proto::pb::catalog_entity_info::SysSnapshotEntryPB;
use yb_proto::pb::master_backup::{SnapshotScheduleOptionsPB, SysRestorationEntryPB};

/// A tablet's outcome this term, not yet written to its row.
#[derive(Clone, Debug, PartialEq)]
pub struct Outcome {
    /// The state the operation was sent for: the outcome applies only to a tablet still in it, so
    /// a create that finishes after a delete started cannot undo the delete.
    pub from: State,
    /// The tablet's new state.
    pub state: State,
    /// The failure, if any.
    pub error: Option<Status>,
}

/// `InitialStateToTerminalState`.
pub fn terminal_state(initial: State, mark_failed: bool) -> State {
    match initial {
        State::CREATING | State::DELETING | State::RESTORING if mark_failed => State::FAILED,
        State::CREATING => State::COMPLETE,
        State::DELETING => State::DELETED,
        State::RESTORING => State::RESTORED,
        other => other,
    }
}

/// The tablets' states with `outcomes` applied.
pub fn merged(
    tablets: impl Iterator<Item = (Vec<u8>, State)>,
    outcomes: Option<&HashMap<Vec<u8>, Outcome>>,
) -> Vec<(Vec<u8>, State)> {
    tablets
        .map(|(id, state)| {
            let state = outcomes
                .and_then(|o| o.get(&id))
                .filter(|o| o.from == state)
                .map_or(state, |o| o.state);
            (id, state)
        })
        .collect()
}

/// `StateWithTablets::AggregatedState`.
pub fn aggregated_state(
    initial: State,
    tablets: &[(Vec<u8>, State)],
    mark_failed: bool,
) -> Result<State, Status> {
    if tablets.is_empty() {
        return Ok(terminal_state(initial, mark_failed));
    }
    let mut result = initial;
    let mut has_initial = false;
    for (_, state) in tablets {
        if *state == State::FAILED {
            return Ok(State::FAILED);
        } else if *state == initial {
            has_initial = true;
        } else if result == initial {
            result = *state;
        } else if *state != result {
            return Err(Status::new(
                StatusCode::IllegalState,
                format!("Tablets in different terminal states: {result:?} and {state:?}"),
            ));
        }
    }
    Ok(if has_initial { initial } else { result })
}

/// `AllTabletsDone`: no tablet is left in the initial state.
pub fn all_done(initial: State, tablets: &[(Vec<u8>, State)]) -> bool {
    tablets.iter().all(|(_, s)| *s != initial)
}

/// The tablets of a snapshot row.
pub fn snapshot_tablets(pb: &SysSnapshotEntryPB) -> impl Iterator<Item = (Vec<u8>, State)> + '_ {
    pb.tablet_snapshots
        .iter()
        .map(|t| (t.id().to_vec(), t.state()))
}

/// The tablets of a restoration row.
pub fn restoration_tablets(
    pb: &SysRestorationEntryPB,
) -> impl Iterator<Item = (Vec<u8>, State)> + '_ {
    pb.tablet_restorations
        .iter()
        .map(|t| (t.id().to_vec(), t.state()))
}

/// `RestorationState::MigrateInitialStateIfNeeded`: rows written before #16131 hold the final
/// state.
pub fn restoration_initial_state(pb: &SysRestorationEntryPB) -> State {
    match pb.state() {
        State::RESTORED | State::FAILED => State::RESTORING,
        other => other,
    }
}

/// `SnapshotScheduleState::deleted`.
pub fn schedule_deleted(options: &SnapshotScheduleOptionsPB) -> bool {
    options.delete_time() != 0
}

/// `SnapshotScheduleState::ValidateOptions`.
pub fn validate_schedule_options(options: &SnapshotScheduleOptionsPB) -> Result<(), Status> {
    let invalid = |m: &str| Err(Status::new(StatusCode::InvalidArgument, m));
    if options.interval_sec() == 0 {
        invalid("Zero interval")
    } else if options.retention_duration_sec() == 0 {
        invalid("Zero retention")
    } else if options.interval_sec() >= options.retention_duration_sec() {
        invalid("Interval must be strictly less than retention")
    } else {
        Ok(())
    }
}

/// `ComputeMinRestoreTime`: the earliest time a snapshot restores to, the previous snapshot's time
/// (the first snapshot of a schedule restores only to its own).
pub fn min_restore_time(pb: &SysSnapshotEntryPB) -> u64 {
    if pb.previous_snapshot_hybrid_time() != 0 {
        pb.previous_snapshot_hybrid_time()
    } else {
        pb.snapshot_hybrid_time()
    }
}
