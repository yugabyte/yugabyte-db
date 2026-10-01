---
title: Rewind to a point in time
headerTitle: Rewind to PIT
linkTitle: Rewind to PIT
description: Rewind a YugabyteDB database to a specific point in time
headcontent: Rewind a database to a specific point in time
menu:
  stable:
    identifier: pitr-rewind
    parent: point-in-time-recovery
    weight: 40
type: docs
---

{{< tip title="Which PITR method should I use?" >}}
To decide which point-in-time feature is right for your use case, refer to [The PIT recovery family](../#the-pit-recovery-family).
{{< /tip >}}

Rewind to PIT rewinds a database or keyspace to an earlier point in time on the original cluster. The entire contents of the database (schema, table data, and more) are rewound. After a rewind, data written in the intervening period is permanently discarded from that database.

Use Rewind when either:

- There were no important writes after the error (for example, a rarely written application database), or
- Intervening writes can be discarded or replayed from an external application-maintained log after the rewind.

If recent writes must be preserved, use [Clone to PIT](../clone/) or [Restore to PIT](../restore/) instead.

## Prerequisites

- [Create a snapshot schedule](../enable-pitr/#create-a-schedule) for the database or keyspace.
- Confirm that no rewind or restore is already in progress for the subject database or keyspace; if multiple rewind commands are issued, the data might enter an inconsistent state.

## Rewind a database or keyspace

If a database or keyspace has an associated snapshot schedule, you can rewind it to a particular point in time using the [restore_snapshot_schedule](../../../../admin/yb-admin/#restore-snapshot-schedule) command with the following parameters:

- The ID of the schedule.
- Target rewind time, with the following two options:

  - Rewind to an absolute time, providing a specific timestamp in one of the following formats:

    - [Unix timestamp](https://www.unixtimestamp.com) in microseconds.
    - [YSQL timestamp](../../../../api/ysql/datatypes/type_datetime/).
    - [YCQL timestamp](../../../../api/ycql/type_datetime/#timestamp).

    For example, the following command rewinds to 1:00 PM PDT on May 1st 2022 using a Unix timestamp:

    ```sh
    ./bin/yb-admin \
        --master_addresses <ip1:7100,ip2:7100,ip3:7100> \
        restore_snapshot_schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256 1681964544554620
    ```

    The following is an equivalent command that uses a YCQL timestamp:

    ```sh
    ./bin/yb-admin \
        --master_addresses <ip1:7100,ip2:7100,ip3:7100> \
        restore_snapshot_schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256 "2022-05-01 13:00-0700"
    ```

  - Rewind to a time that is relative to the current time (for example, to 10 minutes ago) by specifying how far back you would like to roll the database or keyspace.

    For example, to rewind to 5 minutes ago, run the following command:

    ```sh
    ./bin/yb-admin \
        --master_addresses <ip1:7100,ip2:7100,ip3:7100> \
        restore_snapshot_schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256 minus 5m
    ```

    Or, to rewind to 1 hour ago, use the following:

    ```sh
    ./bin/yb-admin \
        --master_addresses <ip1:7100,ip2:7100,ip3:7100> \
        restore_snapshot_schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256 minus 1h
    ```

    For detailed information on the relative time formatting, refer to the [restore_snapshot_schedule reference](../../../../admin/yb-admin/#restore-snapshot-schedule).

{{< note title="YSQL index backfill" >}}

YugabyteDB supports [index backfill](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/online-index-backfill.md), which asynchronously populates a new index. The process runs in the background and can take a significant amount of time, depending on the size of the data. If you rewind to a point in time soon after an index creation, you're likely to hit a state where the index is in the middle of the backfill process.

**YugabyteDB ignores these partly-backfilled indexes during read operations. To make sure the indexes are properly used, you need to drop and create them again to re-initiate the backfill process.** Run the following query to get a list of indexes that need to be recreated:

```sql
SELECT pg_class.relname
    FROM pg_index
    JOIN pg_class
    ON pg_index.indexrelid = pg_class.oid
    WHERE NOT indisvalid;
```

This affects only YSQL databases. For YCQL, YugabyteDB automatically restarts index backfill after the rewind.

See issue {{<issue 12940>}} for details.

{{< /note >}}

## Limitations

Rewind has several limitations, primarily related to interactions with other YugabyteDB features. Most of these limitations will be addressed in upcoming releases; refer to each limitation's corresponding tracking issue for details.

### CDC

For databases and tables with [CDC](../../../../additional-features/change-data-capture/) configured, you need to create new CDC streams or replication slots after the rewind is complete, and start streaming from that point. Creating new streams or slots ensures that you start streaming from the correct checkpoints.

### xCluster replication

xCluster does not replicate any commands related to PITR. If you have two clusters with replication between them, enable PITR on both ends independently. You can perform a rewind using the following recommended procedure:

1. Stop application workloads and make sure there are no active transactions.
1. Wait for replication to complete.
1. Delete xCluster replication from both clusters.
1. Rewind both clusters to the exact same time.
1. Re-establish xCluster replication.
1. Resume the application workloads.

### Global objects

Rewind doesn't restore global objects, such as [tablespaces](../../../../explore/going-beyond-sql/tablespaces/), roles, and permissions, because they're not currently backed up by the distributed snapshots. If you alter or drop a global object, then try to rewind to a point in time before the change, the object will _not_ be restored.

Tracking issue for YSQL tablespaces: [10257](https://github.com/yugabyte/yugabyte-db/issues/10257)

Tracking issue for YSQL roles and permissions: [10349](https://github.com/yugabyte/yugabyte-db/issues/10349)

Tracking issue for YCQL: [8453](https://github.com/yugabyte/yugabyte-db/issues/8453)

{{< note title="Special case for tablespaces" >}}

Tablespaces are crucial for geo-partitioned deployments. Trying to rewind a database that relies on a removed tablespace will lead to unexpected behavior, so the `DROP TABLESPACE` command is currently disallowed if a schedule exists on _any_ of the databases in the cluster.

{{< /note >}}

### YSQL system catalog upgrade

You can't use Rewind to return to a state before the most recent [YSQL system catalog upgrade](../../../../admin/yb-admin/#upgrade-ysql-system-catalog). Trying to do so will produce an error. You can still use [distributed snapshots](../../snapshot-ysql/) to restore in this scenario.

Tracking issue: [13158](https://github.com/yugabyte/yugabyte-db/issues/13158)

This limitation applies only to YSQL databases. YCQL is not affected.

### YugabyteDB Anywhere

YugabyteDB Anywhere [supports Rewind via PITR](../../../../yugabyte-platform/back-up-restore-universes/pitr/).

{{< warning title="Do not mix yb-admin and the YugabyteDB Anywhere UI" >}}

A database or keyspace can have at most one snapshot schedule. On a universe managed by YugabyteDB Anywhere, manage PITR only from the YugabyteDB Anywhere UI. Using yb-admin and the UI together to manage snapshot schedules can cause conflicts and is strongly discouraged. Changes you make using yb-admin are not reflected in YugabyteDB Anywhere.

{{< /warning >}}

### Other limitations

- Rewind via snapshot schedules works with _in-cluster_ distributed snapshots. For restoring from off-cluster backups to a point in time, see [Restore to PIT](../restore/).
- Issuing DDLs against a database while it is being rewound is not recommended.
- Rewinding to a time during which DDLs were in flight may fail or produce inconsistent results. See issue {{<issue 12797>}}.
