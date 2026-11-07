# Tablet splits (`master-tablet-split-manager`)

The leader side of tablet splitting: `CatalogManager::SplitTablet`, `DoSplitTablet` and
`DeleteNotServingTablet`, with the candidate checks of `TabletSplitManager`. Manual splits only so
far; automatic splitting (`TabletSplitManager::DoSplitting`, candidate choice by size and split
phase) is not served yet.

## A split

1. `MasterAdmin.SplitTablet` checks the candidate (`ValidateSplitCandidateTable` and
   `ValidateSplitCandidateTablet` for what a Rust leader serves: not a transaction status,
   system, YEDIS or YSQL table, not one hosting stateful services or covered by a snapshot
   schedule, not colocated, RUNNING, and a split child only once its parent is gone) and answers.
   As C++ schedules `AsyncGetTabletSplitKey`, the leader then asks the tablet's leader for the
   split key (`TabletServerService.GetSplitKey`) in the background.
2. `DoSplitTablet` is one catalog mutation (K1): the children as CREATING tablets (ids sorted,
   the parent's committed config, `split_depth` + 1, `split_parent_tablet_id`), the parent's
   `split_tablet_ids`, and the table's next `partition_list_version`. A tablet that already has
   children resumes with them.
3. The `tablet-splits` controller sends `TabletServerAdminService.SplitTablet` to the leader of
   every parent whose children are not all RUNNING, once per term and again after a failure,
   with the encoded keys from the host (`EncodedPartitionKeys`). A new leader so finishes a split
   the last one registered, as `DoSplitting` restarts splits. The tablet-assignment controller
   neither creates nor replaces split children: the parent's replicas create them.
4. The children become RUNNING from tablet reports. Once they serve, the tserver's split cleanup
   calls `DeleteNotServingTablet`: the parent becomes DELETED if running tablets of its table
   cover its range (`CheckIfCanDeleteSingleTablet`), and each of its replicas is deleted.

## Where it differs from C++

- `CheckIfCanDeleteSingleTablet` accepts a tablet over the whole key space with nothing covering
  it, since its empty start and end compare equal; the Rust check requires a running tablet over
  the range.
- A table a snapshot schedule covers is not split, since its parent would be hidden rather than
  deleted (W10).
