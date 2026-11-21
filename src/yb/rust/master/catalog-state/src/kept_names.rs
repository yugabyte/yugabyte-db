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

use std::collections::HashSet;
use std::sync::{Arc, Mutex, OnceLock};

use master_leadership::{LeaderToken, Leadership, Phase};

/// The namespaces that keep their name in one leader epoch (term and restore count) although their
/// state would give it up: C++ leaves such a namespace in `namespace_names_mapper_` until the next
/// catalog load, which every new term and every PITR restore does. Every by-name lookup reads it
/// through the snapshot, so they all answer by one rule. It lives outside the rows because C++
/// never persists it.
#[derive(Default)]
pub struct KeptNames {
    leadership: OnceLock<Arc<Leadership>>,
    kept: Mutex<Kept>,
}

/// The namespaces kept in one epoch.
#[derive(Default)]
struct Kept {
    /// (term, restore count) of the last keep.
    epoch: (i64, u64),
    ids: HashSet<Vec<u8>>,
}

impl KeptNames {
    /// The leadership whose current epoch [`KeptNames::keeps`] checks; until it is set, nothing is
    /// kept. Only the first call has an effect.
    pub fn set_leadership(&self, leadership: Arc<Leadership>) {
        let _ = self.leadership.set(leadership);
    }

    /// Keeps the name of `namespace_id` for the rest of `token`'s epoch. A keep of an earlier
    /// epoch is ignored.
    pub fn keep(&self, token: &LeaderToken, namespace_id: &[u8]) {
        let epoch = (token.term(), token.pitr_count());
        let mut kept = self.kept.lock().unwrap();
        if kept.epoch < epoch {
            *kept = Kept {
                epoch,
                ids: HashSet::new(),
            };
        }
        if kept.epoch == epoch {
            kept.ids.insert(namespace_id.to_vec());
        }
    }

    /// Whether the name of `namespace_id` was kept in the leader's current epoch.
    pub fn keeps(&self, namespace_id: &[u8]) -> bool {
        let epoch = match self.leadership.get().map(|l| l.phase()) {
            Some(Phase::Ready { term, pitr_count })
            | Some(Phase::LeaseLapsed {
                term, pitr_count, ..
            }) => (term, pitr_count),
            _ => return false,
        };
        let kept = self.kept.lock().unwrap();
        kept.epoch == epoch && kept.ids.contains(namespace_id)
    }
}
