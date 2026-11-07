# Tablet splits (`master-tablet-split-manager`)

The leader side of tablet splitting: `CatalogManager::SplitTablet`, `DoSplitTablet` and
`DeleteNotServingTablet`, the candidate checks of `TabletSplitManager`, and automatic splitting
(`TabletSplitManager::DoSplitting`).

## A split

1. `MasterAdmin.SplitTablet` checks the candidate (`ValidateSplitCandidateTable` and
   `ValidateSplitCandidateTablet` for what a Rust leader serves: not a transaction status,
   system, YEDIS or YSQL table, not one hosting stateful services, covered by a snapshot
   schedule, in an xCluster or CDC stream or backfilling an index, not colocated, RUNNING, and a
   split child only once its parent is gone), then the table limits
   (`max_create_tablets_per_ts` per live tserver, `tablet_split_limit_per_table`), and answers.
   As C++ schedules `AsyncGetTabletSplitKey`, the leader then asks the tablet's leader for the
   split key (`TabletServerService.GetSplitKey`) in the background.
2. `DoSplitTablet` is one catalog mutation (K1): the children as CREATING tablets (ids sorted,
   the parent's committed config, `split_depth` + 1, `split_parent_tablet_id`), the parent's
   `split_tablet_ids`, and the table's next `partition_list_version`. A tablet that already has
   children resumes with them. An automatic split is checked again first
   (`ShouldSplitValidCandidate`), since the table may have changed phase or gained a replica
   since it was picked; a resumed one is not, so a registered split is always finished.
3. The `tablet-splits` controller sends `TabletServerAdminService.SplitTablet` to the leader of
   every parent whose children are not all RUNNING, once per term and again after a failure,
   with the encoded keys from the host (`EncodedPartitionKeys`). A new leader so finishes a split
   the last one registered, as `DoSplitting` restarts splits. The tablet-assignment controller
   neither creates nor replaces split children: the parent's replicas create them.
4. The children become RUNNING from tablet reports. Once they serve, the tserver's split cleanup
   calls `DeleteNotServingTablet`: the parent becomes DELETED if running tablets of its table
   cover its range (`CheckIfCanDeleteSingleTablet`), and each of its replicas is deleted.

## Automatic splitting

Each `tablet-splits` pass (every `catalog_manager_bg_task_wait_ms`, at most every
`process_split_tablet_candidates_interval_msec`) runs `auto::plan`, a pure function of the
catalog snapshot, the reported replicas and the live tservers, unless
`enable_automatic_tablet_splitting` is off or a feature disabled splitting
(`DisableTabletSplitting`). It returns the new splits; each is started as a manual one is, with
`is_manual_split` false.

- **Candidates.** A RUNNING tablet of a valid table whose leader reports SST files above its
  table's phase threshold (`tablet_split_{low,high}_phase_size_threshold_bytes`,
  `tablet_force_split_threshold_bytes`), with exactly RF running voters and none bootstrapping,
  and no replica that may still hold its parent's data (`may_have_orphaned_post_split_data`,
  which defaults to true until a metrics heartbeat says otherwise). The phase comes from the
  table's partitions per live, non-blacklisted tserver its placement allows.
- **Outstanding splits.** Splits being started, registered splits whose children are not all
  running, and splits whose children are still compacting count against
  `outstanding_tablet_split_limit` and, on each tserver of the parent and children,
  `outstanding_tablet_split_limit_per_tserver` (negative: 1 on up to 4 CPUs, else 2).
- **Order.** Candidates below `tablet_split_min_size_ratio` of their table's largest wait; low
  phase goes first, then the largest.
- **Held off.** A table with a default TTL whose leader's tserver asks not to split such tables
  (metrics `disable_tablet_split_if_default_ttl`), or whose `GetSplitKey` answered
  `TABLET_SPLIT_DISABLED_TTL_EXPIRY`, waits `prevent_split_for_ttl_tables_for_seconds`; a tablet
  answered `TABLET_SPLIT_KEY_RANGE_TOO_SMALL` waits
  `prevent_split_for_small_key_range_tablets_for_seconds`. These lists and the feature list live
  in the master's manager, not the term, as in C++.

`IsTabletSplittingComplete` is true when no split is being started, no split child is not yet
running and, if asked, no split parent is left undeleted. It takes the manager's lock a pass
holds while picking splits, so a pass that began before `DisableTabletSplitting` has started its
splits by the time it answers (`WaitUntilIdle`).

## Where it differs from C++

- `CheckIfCanDeleteSingleTablet` accepts a tablet over the whole key space with nothing covering
  it, since its empty start and end compare equal; the Rust check requires a running tablet over
  the range.
- A table a snapshot schedule covers is not split, since its parent would be hidden rather than
  deleted (W10).
- A table in an xCluster or CDC stream is not split: the streams' tablet mappings would need
  updating (`ProcessSplitTabletResult`), which a Rust leader does not do.
- C++ evaluates a parent with registered children as a fresh candidate, and a later
  `DoSplitTablet` resumes it; the Rust pass leaves registered splits to the `tablet-splits`
  controller and counts them only against the limits.
- Backfill is read from the indexes' permissions (`INDEX_PERM_DO_BACKFILL`), since a Rust leader
  keeps no in-memory backfill flag.
- The `tablet_split_candidates` and `outstanding_tablet_splits` gauges are logged at debug
  level rather than exported.
