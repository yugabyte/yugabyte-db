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

//! `AddTransactionStatusTablet`, and the scale-up of transaction status tables as the live
//! tservers grow (`ScaleUpTransactionStatusTablesIfNeeded`).

use master_catalog_state::Mutation;
use master_leadership::LeaderToken;
use protobuf::Message;
use tokio::time::Instant;
use yb_host_api::StatusCode;
use yb_proto::pb::catalog_entity_info::{SysTablesEntryPB, SysTabletsEntryPB};
use yb_proto::pb::common::PartitionPB;
use yb_proto::pb::common_types::TableType;
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};

use crate::create::{add_new_tablets, SYSTEM_NAMESPACE_ID, TRANSACTIONS_TABLE};
use crate::lookup::{find_table, is_visible_to_client};
use crate::placement::{cluster_config, in_placement, live_tservers};
use crate::{error, TableLifecycle};

/// A hash partition's bounds, inclusive (`DecodePartitionAsHashBoundsInclusive`).
fn hash_bounds(partition: &PartitionPB) -> Option<(u16, u16)> {
    let decode = |key: &[u8]| -> Option<u16> { Some(u16::from_be_bytes(key.try_into().ok()?)) };
    let start = match partition.partition_key_start() {
        [] => 0,
        key => decode(key)?,
    };
    let end = match partition.partition_key_end() {
        [] => u16::MAX,
        key => decode(key)?.checked_sub(1)?,
    };
    (start <= end).then_some((start, end))
}

/// `SplitHashPartitionForStatusTablet`: the two halves of a hash partition wider than one value.
pub(crate) fn split_status_partition(
    partition: &PartitionPB,
) -> Option<(PartitionPB, PartitionPB)> {
    let (start, end) = hash_bounds(partition)?;
    if start == end {
        return None;
    }
    let mid = ((end - start) / 2 + start + 1).to_be_bytes().to_vec();
    let mut left = PartitionPB::new();
    left.set_partition_key_start(partition.partition_key_start().to_vec());
    left.set_partition_key_end(mid.clone());
    let mut right = PartitionPB::new();
    right.set_partition_key_start(mid);
    right.set_partition_key_end(partition.partition_key_end().to_vec());
    Some((left, right))
}

impl TableLifecycle {
    /// `AddTransactionStatusTablet`: halves the first serving tablet's hash partition whose range
    /// can be split, and adds a PREPARING tablet for the right half. The shrunk tablet and the new
    /// one are written together, so a new leader sees both or neither. The tablet's tservers keep
    /// its old partition, which a status tablet does not use.
    pub async fn add_transaction_status_tablet(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        table_id: &[u8],
    ) -> Result<(), MasterErrorPB> {
        let corrupt = |e: String| error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e);
        let mut ident = yb_proto::pb::master_types::TableIdentifierPB::new();
        ident.set_table_id(table_id.to_vec());
        self.write(token, deadline, |snapshot| {
            let table = find_table(snapshot, &ident, false)?;
            for tablet in snapshot.active_tablets_of(table_id) {
                let mut old: SysTabletsEntryPB =
                    tablet.parse().map_err(|e| corrupt(e.to_string()))?;
                let Some((left, right)) = split_status_partition(&old.partition) else {
                    continue;
                };
                old.partition = protobuf::MessageField::some(left);
                let mutation = Mutation::new().read(&table).read(tablet).upsert(
                    SysRowEntryType::TABLET,
                    &tablet.key.id,
                    old.write_to_bytes().map_err(|e| corrupt(e.to_string()))?,
                );
                let mutation = add_new_tablets(
                    snapshot,
                    mutation,
                    table_id,
                    &[right],
                    (self.options.use_parent_table_id_field)(),
                    false,
                )?;
                return Ok((mutation, ()));
            }
            Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::NotFound,
                format!(
                    "Table {} has no splittable hash partition",
                    String::from_utf8_lossy(table_id)
                ),
            ))
        })
        .await?;
        tracing::info!(
            "Added a transaction status tablet to table {}",
            String::from_utf8_lossy(table_id)
        );
        Ok(())
    }

    /// `DoScaleUpTransactionStatusTables`: each transaction status table gets tablets until it
    /// has `transaction_table_num_tablets`, else `transaction_table_num_tablets_per_tserver` per
    /// live tserver: all of them for `system.transactions`, those in its placement for a
    /// tablespace's. There is no scale-down.
    pub(crate) async fn scale_up_transaction_status_tables(
        &self,
        token: &LeaderToken,
        num_live_tservers: usize,
    ) -> Result<(), MasterErrorPB> {
        let snapshot = self.catalog.snapshot();
        let global = snapshot
            .table_by_name(SYSTEM_NAMESPACE_ID, TRANSACTIONS_TABLE)
            .ok_or_else(|| {
                error(
                    Code::UNKNOWN_ERROR,
                    StatusCode::NotFound,
                    "Global transaction status table not found",
                )
            })?
            .key
            .id
            .clone();
        let config = cluster_config(&snapshot);
        let mut targets = vec![(global.clone(), num_live_tservers)];
        for table in snapshot.iter(SysRowEntryType::TABLE) {
            if table.key.id == global {
                continue;
            }
            let Ok(pb) = table.parse::<SysTablesEntryPB>() else {
                continue;
            };
            if pb.table_type() != TableType::TRANSACTION_STATUS_TABLE_TYPE
                || !is_visible_to_client(table)
            {
                continue;
            }
            let placement =
                match self
                    .tablespaces
                    .table_replication_info(&table.key.id, &pb, &config)
                {
                    Ok(info) => info.live_replicas.unwrap_or_default(),
                    Err(e) => {
                        tracing::warn!(
                            "Failed to find the placement of transaction status table {}: {}",
                            String::from_utf8_lossy(&table.key.id),
                            e.message
                        );
                        continue;
                    }
                };
            let live = live_tservers(&self.ts_manager, &config, b"");
            let tservers = in_placement(&live, &placement);
            if tservers == 0 {
                tracing::warn!(
                    "No live tservers for the placement of transaction status table {}",
                    String::from_utf8_lossy(&table.key.id)
                );
                continue;
            }
            targets.push((table.key.id.clone(), tservers));
        }
        for (table_id, tservers) in targets {
            let tablets = self.catalog.snapshot().active_tablets_of(&table_id).len();
            let expected = match (self.options.transaction_table_num_tablets)() {
                n if n > 0 => n as usize,
                _ => {
                    tservers
                        * (self.options.transaction_table_num_tablets_per_tserver)().max(0) as usize
                }
            };
            if tablets >= expected {
                continue;
            }
            tracing::info!(
                "Transaction status table {}: {tablets} tablets for {tservers} live tservers, \
                 adding {}",
                String::from_utf8_lossy(&table_id),
                expected - tablets
            );
            for _ in tablets..expected {
                let deadline = Instant::now() + std::time::Duration::from_secs(60);
                self.add_transaction_status_tablet(token, deadline, &table_id)
                    .await?;
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn partition(start: &[u8], end: &[u8]) -> PartitionPB {
        let mut p = PartitionPB::new();
        p.set_partition_key_start(start.to_vec());
        p.set_partition_key_end(end.to_vec());
        p
    }

    #[test]
    fn splits_hash_partitions_as_cpp() {
        // The whole range [0, 0xFFFF]: mid = 0x7FFF + 1.
        let (l, r) = split_status_partition(&partition(b"", b"")).unwrap();
        assert_eq!(l.partition_key_start(), b"");
        assert_eq!(l.partition_key_end(), &[0x80, 0x00]);
        assert_eq!(r.partition_key_start(), &[0x80, 0x00]);
        assert_eq!(r.partition_key_end(), b"");
        // [0x8000, 0xFFFF]: mid = 0x3FFF + 0x8000 + 1.
        let (l, r) = split_status_partition(&partition(&[0x80, 0x00], b"")).unwrap();
        assert_eq!(l.partition_key_end(), &[0xC0, 0x00]);
        assert_eq!(r.partition_key_start(), &[0xC0, 0x00]);
        // Two values: [5, 6] -> [5, 6) and [6, 7).
        let (l, r) = split_status_partition(&partition(&[0, 5], &[0, 7])).unwrap();
        assert_eq!(l.partition_key_end(), &[0, 6]);
        assert_eq!(r.partition_key_start(), &[0, 6]);
        // One value cannot be split.
        assert!(split_status_partition(&partition(&[0, 5], &[0, 6])).is_none());
        // Not a hash partition.
        assert!(split_status_partition(&partition(b"abc", b"")).is_none());
    }
}
