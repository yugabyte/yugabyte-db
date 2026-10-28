---
title: Restore to a point in time
headerTitle: Restore to PIT
linkTitle: Restore to PIT
description: Restore from a YugabyteDB snapshot or backup to a specific point in time
headcontent: Restore from a YugabyteDB snapshot to a specific point in time
menu:
  stable:
    identifier: pitr-restore
    parent: point-in-time-recovery
    weight: 50
type: docs
---

{{< tip title="Which PITR method should I use?" >}}
To decide which point-in-time feature is right for your use case, refer to [The PIT recovery family](../#the-pit-recovery-family).
{{< /tip >}}

Restore to PIT restores from a backup or snapshot to a user-specified point in time. Unlike [Rewind to PIT](../rewind/), which rewinds the live database in place using a snapshot schedule, Restore to PIT starts from a snapshot or backup and applies flashback to the chosen time. You can restore on the original cluster or on an alternate cluster.

{{< warning title="Advanced workflow" >}}
Moving a distributed snapshot between clusters is a multi-step procedure (for YSQL, it includes a `ysql_dump` of the schema).

In addition, `restore_snapshot` does not _prevent_ you from choosing an unsafe restore time. Restoring across a DDL boundary, or to a time outside the snapshot's retained history, can produce unexpected results. There is no check that the target time is recoverable.

Prefer [Rewind to PIT](../rewind/) or [Clone to PIT](../clone/) for in-cluster recovery.
{{< /warning >}}

{{< tip title="Use YugabyteDB Anywhere" >}}
YugabyteDB Anywhere provides managed Restore to PIT using PITR-enabled backups. You can restore to the original universe or to an alternate universe, and optionally rename databases or keyspaces, with guardrails for DDL changes and history retention. See [Restore universe data](../../../../yugabyte-platform/back-up-restore-universes/restore-universe-data/#restore-from-backup).
{{< /tip >}}

## Restore to PIT on a manually managed cluster

You should use this only if you are building a custom backup and restore workflow and cannot use YugabyteDB Anywhere.

{{< note title="DDL boundary and history retention" >}}

You cannot restore to a point in time earlier than the most recent DDL change that precedes the snapshot. If a DDL change (for example, `DROP TABLE`) occurs at time t1 and a snapshot is taken at t2, you cannot restore to a time before t1.

A single snapshot also cannot recover data outside its history retention window. The snapshot retains change history only for a limited period before t2 (see [history retention](../enable-pitr/#configuration-details)), so it may not include all data written after t1. Restore only to a time that is both after that DDL and within the snapshot's retained history.

**Mitigation:** Before making DDL changes, take a manual ad hoc snapshot or backup.

{{< /note >}}

For restoring to the snapshot creation time only (no point-in-time target), see [Restore a snapshot](../../snapshot-ysql/#restore-a-snapshot).

### Restore on the same cluster

If the snapshot is already on the cluster, use [restore_snapshot](../../../../admin/yb-admin/#restore-snapshot). The optional restore target selects the point in time; omit it to restore to the snapshot's creation time.

```sh
yb-admin \
    --master_addresses <master-addresses> \
    restore_snapshot <snapshot-id> [<restore-target>]
```

- *snapshot-id*: The identifier for the snapshot.
- *restore-target*: Optional. A timestamp at or before the snapshot's creation time. Absolute Unix timestamp in microseconds, or a relative time such as `minus 5m`.

For example:

```sh
./bin/yb-admin \
    --master_addresses <ip1:7100,ip2:7100,ip3:7100> \
    restore_snapshot 72ad2eb1-65a2-4e88-a448-7ef4418bc469 1681964544554620
```

```sh
./bin/yb-admin \
    --master_addresses <ip1:7100,ip2:7100,ip3:7100> \
    restore_snapshot 72ad2eb1-65a2-4e88-a448-7ef4418bc469 minus 5m
```

When the restore starts, the `snapshot_id` and a generated `restoration_id` are displayed. Check progress using [list_snapshots](../../../../admin/yb-admin/#list-snapshots) or [list_snapshot_restorations](../../../../admin/yb-admin/#list-snapshot-restorations).

### Restore on another cluster

Importing a distributed snapshot into a different cluster is not a single command. You must export metadata, copy tablet snapshot files from every node, recreate schema, import the snapshot, and remap tablet IDs before you can call `restore_snapshot`.

For **YSQL**, that workflow includes a schema-only [ysql_dump](../../../../admin/ysql-dump) taken at the same catalog version as the snapshot. Without that dump, you cannot recreate the database on the target cluster.

Follow the full procedures, then pass a restore target on the final `restore_snapshot` if you need a time other than the snapshot's creation time:

1. [Move a snapshot to external storage](../../snapshot-ysql/#move-a-snapshot-to-external-storage) (YSQL: includes `yb_catalog_version` checks and `ysql_dump`).
1. [Restore a snapshot from external storage](../../snapshot-ysql/#restore-a-snapshot-from-external-storage) (YSQL: apply the dump with ysqlsh, then `import_snapshot`, then copy tablet files using the old-to-new ID mapping).
1. Run `restore_snapshot` as shown in [Restore on the same cluster](#restore-on-the-same-cluster).

The YCQL procedure is the same shape without the dump; see [Distributed snapshots for YCQL](../../snapshot-ycql/#restore-a-snapshot-from-external-storage).

### Surgical recovery

When recent production writes must be preserved and you cannot [clone](../clone/) on the production cluster, a typical recovery is:

1. Restore the snapshot or backup to an alternate cluster as of a time just before the error.
1. Optionally use [Inspect at PIT](../inspect/) on that restored database to search across candidate times.
1. Extract the needed rows or objects.
1. Import and merge them back into the production database.

On a manually managed cluster, step 1 is the [off-cluster restore procedure](#restore-on-another-cluster). In YugabyteDB Anywhere, restore the PITR-enabled backup to another universe instead; see [Restore from backup](../../../../yugabyte-platform/back-up-restore-universes/restore-universe-data/#restore-from-backup).
