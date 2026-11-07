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

//! `CalculateOptimalLoadDistribution` and `CalculateTableLoadDifference`: the closest balanced
//! distribution of a table's replicas, for the progress metrics only.

use std::collections::HashMap;

use yb_proto::pb::common_net::PlacementInfoPB;

use crate::{matches_cloud_info, TsInfo};

/// Replica count per tserver uuid.
pub type TsTableLoadMap = HashMap<String, usize>;

/// Spreads `num_replicas` over `entries[start..end]`, extra ones on the more loaded tservers.
fn distribute(
    current: &TsTableLoadMap,
    entries: &mut [(String, usize)],
    start: usize,
    end: usize,
    num_replicas: usize,
) {
    let load = |uuid: &str| current.get(uuid).copied().unwrap_or(0);
    entries[start..end].sort_by(|a, b| load(&b.0).cmp(&load(&a.0)));
    let num_tservers = end - start;
    if num_tservers == 0 {
        return;
    }
    let per_tserver = num_replicas / num_tservers;
    let extra = num_replicas % num_tservers;
    for (i, entry) in entries[start..end].iter_mut().enumerate() {
        entry.1 = per_tserver + usize::from(i < extra);
    }
}

/// Block minimums first, spread evenly over each block's tservers, then the rest of the replicas
/// "water-filled" onto the least loaded tservers. `valid_tservers` excludes blacklisted ones; disk
/// space is not considered.
pub fn calculate_optimal_load_distribution(
    valid_tservers: &[&TsInfo],
    placement: &PlacementInfoPB,
    current: &TsTableLoadMap,
    num_tablets: usize,
) -> Result<TsTableLoadMap, String> {
    let num_replicas = placement.num_replicas().max(0) as usize;
    if valid_tservers.len() < num_replicas {
        return Err(format!(
            "Not enough tservers to host the required number of replicas (have {}, need {})",
            valid_tservers.len(),
            num_replicas
        ));
    }
    let mut entries: Vec<(String, usize)> = Vec::new();
    let mut slack = num_replicas * num_tablets;
    for block in &placement.placement_blocks {
        let min = block.min_num_replicas().max(0) as usize;
        let block_replicas = min * num_tablets;
        slack = slack.saturating_sub(block_replicas);
        let start = entries.len();
        for ts in valid_tservers {
            if matches_cloud_info(&ts.cloud_info, &block.cloud_info) {
                entries.push((ts.uuid.clone(), 0));
            }
        }
        let end = entries.len();
        if end - start < min {
            return Err(format!(
                "Not enough tservers to host the minimum number of replicas in placement block \
                 '{}' (have {}, need {})",
                protobuf::text_format::print_to_string(block),
                end - start,
                min
            ));
        }
        distribute(current, &mut entries, start, end, block_replicas);
    }
    if slack > 0 {
        entries.sort_by_key(|e| e.1);
        // The slack fills the least loaded prefix up to the next tserver's load, like water.
        let (mut prefix_load, mut i) = (0, 0);
        while i < entries.len() {
            let load = entries[i].1;
            if i * load >= prefix_load + slack {
                break;
            }
            prefix_load += load;
            i += 1;
        }
        distribute(current, &mut entries, 0, i, prefix_load + slack);
    }
    Ok(entries.into_iter().collect())
}

/// The replicas that must be added to reach `goal`; removes are cheap and not counted.
pub fn calculate_table_load_difference(current: &TsTableLoadMap, goal: &TsTableLoadMap) -> usize {
    goal.iter()
        .map(|(uuid, goal)| goal.saturating_sub(current.get(uuid).copied().unwrap_or(0)))
        .sum()
}
