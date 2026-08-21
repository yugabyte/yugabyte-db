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

Restore to PIT restores from a [distributed snapshot](../../snapshot-ysql/) or backup to a user-specified point in time. Unlike [Rewind to PIT](../rewind/), which rewinds the live database in place using a snapshot schedule, Restore to PIT starts from a snapshot (in-cluster or imported from external storage) and applies flashback to the chosen time.

Use Restore to PIT when:

- Policy requires forensic recovery on an *alternate* cluster (not production).
- You need a longer recoverability window stored on cheaper backup storage.
- You are restoring a snapshot that was [moved to external storage](../../snapshot-ysql/#move-a-snapshot-to-external-storage) into the same or a different cluster.

{{< note title="DDL boundary limitation" >}}

You cannot restore to a point in time earlier than the most recent DDL change that precedes the snapshot. If a DDL change (for example, `DROP TABLE`) occurs at time t1 and a snapshot is taken at t2, you can restore to any time between t1 and t2, but not before t1.

**Mitigation:** Before making DDL changes, take a manual ad hoc snapshot or backup.

{{< /note >}}

## Prerequisites

- A completed [distributed snapshot](../../snapshot-ysql/#create-a-snapshot) of the database or keyspace. For off-cluster restore, [export the snapshot](../../snapshot-ysql/#move-a-snapshot-to-external-storage) and [import](../../snapshot-ysql/#restore-a-snapshot-from-external-storage) it on the target cluster first.
- The target restore time must fall within the history retained by that snapshot (the snapshot's creation time and the history retention covering times before it, subject to the DDL boundary).

## Restore a snapshot to a point in time

Use the [restore_snapshot](../../../../admin/yb-admin/#restore-snapshot) command. The optional restore target selects the point in time; omit it to restore to the snapshot's creation time.

```sh
yb-admin \
    --master_addresses <master-addresses> \
    restore_snapshot <snapshot-id> <restore-target>
```

- *snapshot-id*: The identifier for the snapshot.
- *restore-target*: The time to which to restore. This can be an absolute Unix timestamp in microseconds, or a relative time such as `minus 5m`. Optional; omit to restore to the snapshot's creation time.

The following example uses an absolute time:

```sh
./bin/yb-admin \
    --master_addresses <ip1:7100,ip2:7100,ip3:7100> \
    restore_snapshot 72ad2eb1-65a2-4e88-a448-7ef4418bc469 1681964544554620
```

The following example uses a relative time:

```sh
./bin/yb-admin \
    --master_addresses <ip1:7100,ip2:7100,ip3:7100> \
    restore_snapshot 72ad2eb1-65a2-4e88-a448-7ef4418bc469 minus 5m
```

When the restore starts, the `snapshot_id` and a generated `restoration_id` are displayed.

Check progress using [list_snapshots](../../../../admin/yb-admin/#list-snapshots) or [list_snapshot_restorations](../../../../admin/yb-admin/#list-snapshot-restorations).

For restoring to the snapshot creation time only (no point-in-time target), see also [Restore a snapshot](../../snapshot-ysql/#restore-a-snapshot).

## Surgical recovery workflow

A typical Restore to PIT recovery when recent production writes must be preserved looks like this:

1. Restore the snapshot or backup to an alternate cluster (or into a new database) as of a time just before the error.
1. Optionally use [Inspect at PIT](../inspect/) on that restored database to search across candidate times.
1. Extract the needed rows or objects.
1. Import and merge them back into the production database.

This is the preferred path when policy does not allow creating a [clone](../clone/) on the production cluster.

## YugabyteDB Anywhere

YugabyteDB Anywhere provides a managed experience for Restore to PIT using PITR-enabled backups:

1. Create a [scheduled backup with ability to restore to point-in-time](../../../../yugabyte-platform/back-up-restore-universes/schedule-data-backups/).
1. [Restore the backup](../../../../yugabyte-platform/back-up-restore-universes/restore-universe-data/#restore-a-pitr-enabled-backup) and select **An earlier point in time** to choose any moment in the backup's restore window.

You can restore to the original universe or to an alternate universe, and optionally rename databases or keyspaces. The same DDL boundary limitation applies.
