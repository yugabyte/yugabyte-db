---
title: Point-in-Time Recovery
headerTitle: Point-in-time recovery
linkTitle: Point-in-time recovery
description: Restore data from a specific point in time in YugabyteDB
menu:
  v2.12:
    identifier: point-in-time-recovery
    parent: backup-restore
    weight: 704
type: docs
---

Point-in-time recovery (PITR) in YugabyteDB enables recovery from a user or software error, while minimizing recovery point objective (RPO), recovery time objective (RTO), and overall impact on the cluster.

PITR is particularly applicable to the following:

* DDL errors, such as an accidental table removal.
* DML errors, such as execution of an incorrect update statement against one of the tables.

Typically, you know when the data was corrupted and would want to restore to the closest possible uncorrupted state. With PITR, you can achieve that by providing a timestamp to which to restore. You can specify the time with the precision of up to 1 microsecond, far more precision than is possible with the regular snapshots that are typically taken hourly or daily.

PITR in YugabyteDB is based on a combination of the flashback capability and periodic [distributed snapshots](../snapshot-ysql).

Flashback provides a way to rewind the data back in time. At any moment, YugabyteDB stores not only the latest state of the data, but also the recent history of changes. With flashback, you can rollback to any point in time in the history retention period. The history is also preserved when a snapshot is taken, which means that by creating snapshots periodically, you effectively increase the flashback retention.

For example, if your overall retention target for PITR is three days, you can use the following configuration:

* History retention interval is 24 hours.
* Snapshots are taken daily.
* Each snapshot is kept for three days.

By default, the history retention period is controlled by the [history retention interval flag](../../../reference/configuration/yb-tserver/#timestamp-history-retention-interval-sec) applied cluster-wide to every YSQL database and YCQL keyspace.

However, when [PITR is enabled](#create-a-schedule) for a database or a keyspace, YugabyteDB adjusts the history retention for that database or keyspace based on the interval between the snapshots. You are not required to manually set the cluster-wide flag in order to use PITR.

There are no technical limitations on the retention target. However, when you increase the number of stored snapshots, you also increase the amount of space required for the database. The actual overhead depends on the workload, therefore it is recommended to estimate it by running tests based on your applications.

The preceding sample configuration ensures that at any moment there is a continuous change history maintained for the last three days. When you trigger a restore, YugabyteDB selects the closest snapshot to the timestamp you provide, and then uses flashback in that snapshot.

For example, snapshots are taken daily at 11:00 PM, current time is 5:00 PM on April 14th, and you want to restore to 3:00 PM on April 12th. YugabyteDB performs the following:

1. Locates the snapshot taken on April 12th, which is the closest snapshot taken after the restore time, and restores that snapshot.
1. Flashes back 8 hours to restore to the state at 3:00 PM, as opposed to 11:00 PM, which is when the snapshot was taken.

![Point-In-Time Recovery](/images/manage/backup-restore/pitr.png)

## Enable and disable PITR

YugabyteDB exposes the PITR functionality through a set of [snapshot schedule](../../../admin/yb-admin/#backup-and-snapshot-commands) commands. A schedule is an entity that automatically manages periodic snapshots for a YSQL database or a YCQL keyspace, and enables PITR for the same database or keyspace.

Creating a snapshot schedule for a database or a keyspace effectively enables PITR for that database or keyspace. You cannot recover to point in time unless you create a schedule.

### Create a schedule

To create a schedule and enable PITR, use the [`create_snapshot_schedule`](../../../admin/yb-admin/#create-snapshot-schedule) command with the following parameters:

* Interval between snapshots (in minutes).
* Retention time for every snapshot (in minutes).
* The name of the database or keyspace.

Assuming the retention target is three days, you can execute the following command to create a schedule that produces a snapshot once a day (every 1,440 minutes) and retains it for three days (4,320 minutes):

```sh
./bin/yb-admin -master_addresses <ip1:7100,ip2:7100,ip3:7100> create_snapshot_schedule 1440 4320 <database_name>
```

The following output is a unique ID of the newly-created snapshot schedule:

```output.json
{
  "schedule_id": "6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256"
}
```

You can use this ID to [delete the schedule](#delete-a-schedule) or [restore to a point in time](#restore-to-a-point-in-time).

### Delete a schedule

To delete a schedule and disable PITR, use the following [`delete_snapshot_schedule`](../../../admin/yb-admin/#delete-snapshot-schedule) command that takes the ID of the schedule to be deleted as a parameter:

```sh
./bin/yb-admin -master_addresses <ip1:7100,ip2:7100,ip3:7100> delete_snapshot_schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256
```

### List schedules

To see a list of schedules that currently exist in the cluster, use the following [`list_snapshot_schedules`](../../../admin/yb-admin/#list-snapshot-schedules) command:

```sh
./bin/yb-admin -master_addresses <ip1:7100,ip2:7100,ip3:7100> list_snapshot_schedules
```

```output.json
{
  "schedules": [
    {
      "id": "6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256",
      "options": {
        "interval": "60.000s",
        "retention": "600.000s"
      },
      "snapshots": [
        {
          "id": "386740da-dc17-4e4a-9a2b-976968b1deb5",
          "snapshot_time_utc": "2021-04-28T13:35:32.499002+0000"
        },
        {
          "id": "aaf562ca-036f-4f96-b193-f0baead372e5",
          "snapshot_time_utc": "2021-04-28T13:36:37.501633+0000",
          "previous_snapshot_time_utc": "2021-04-28T13:35:32.499002+0000"
        }
      ]
    }
  ]
}
```

You can also use the same command to view the information about a particular schedule by providing its ID as a parameter, as follows:

```sh
./bin/yb-admin -master_addresses <ip1:7100,ip2:7100,ip3:7100> list_snapshot_schedules 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256
```

## Restore to a point in time

{{< warning title="Stop workloads before restoring" >}}

Stop all the application workloads before you restore to a point in time. Transactions running concurrently with the restore operation can lead to data inconsistency.

This requirement will be removed in an upcoming release, and is tracked in issue [12853](https://github.com/yugabyte/yugabyte-db/issues/12853).

{{< /warning >}}

If a database or a keyspace has an associated snapshot schedule, you can use that schedule to restore the database or keyspace to a particular point in time by using the [`restore_snapshot_schedule`](../../../admin/yb-admin/#restore-snapshot-schedule) command with the following parameters:

* The ID of the schedule.

* Target restore time, with the following two options:

  * Restore to an absolute time, providing a specific timestamp in one of the following formats:

    * [Unix timestamp](https://www.unixtimestamp.com) in seconds, milliseconds, or microseconds.
    * [YSQL timestamp](../../../api/ysql/datatypes/type_datetime/).
    * [YCQL timestamp](../../../api/ycql/type_datetime/#timestamp).

    For example, the following command restores to 1:00 PM PDT on May 1st 2022 using a Unix timestamp:

    ```sh
    ./bin/yb-admin \
        -master_addresses <ip1:7100,ip2:7100,ip3:7100> \
        restore_snapshot_schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256 1651435200
    ```

    The following is an equivalent command that uses a YCQL timestamp:

    ```sh
    ./bin/yb-admin \
        -master_addresses <ip1:7100,ip2:7100,ip3:7100> \
        restore_snapshot_schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256 "2022-05-01 13:00-0700"
    ```

  * Restore to a time that is relative to the current (for example, to 10 minutes ago from now) by specifying how much time back you would like to roll a database or keyspace.

    For example, to restore to 5 minutes ago, run the following command:

    ```sh
    ./bin/yb-admin \
        -master_addresses <ip1:7100,ip2:7100,ip3:7100> \
        restore_snapshot_schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256 minus 5m
    ```

    Or, to restore to 1 hour ago, use the following:

    ```sh
    ./bin/yb-admin \
        -master_addresses <ip1:7100,ip2:7100,ip3:7100> \
        restore_snapshot_schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256 minus 1h
    ```

    For detailed information on the relative time formatting, refer to the [`restore_snapshot_schedule` reference](../../../admin/yb-admin/#restore-snapshot-schedule).

{{< note title="YSQL index backfill" >}}

YugabyteDB supports [index backfill](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/online-index-backfill.md), which asynchronously populates a new index. The process runs in the background and can take a significant amount of time, depending on the size of the data. If you restore to a point in time soon after an index creation, you're likely to hit a state where the index is in the middle of the backfill process.

**YugabyteDB ignores these partly-backfilled indexes during read operations. To make sure the indexes are properly used, you need to drop and create them again to reinitiate the backfill process.** Run the following query to get a list of indexes that need to be recreated:

```sql
SELECT pg_class.relname
    FROM pg_index
    JOIN pg_class
    ON pg_index.indexrelid = pg_class.oid
    WHERE NOT indisvalid;
```

This affects only YSQL databases. For YCQL, YugabyteDB automatically restarts index backfill after the restore.

This limitation will be removed in an upcoming release, and is tracked in issue [12672](https://github.com/yugabyte/yugabyte-db/issues/12672).

{{< /note >}}

## Limitations

PITR functionality has several limitations, primarily related to interactions with other YugabyteDB features. Most of these limitations will be addressed in upcoming releases; refer to each limitation's corresponding tracking issue for details.

### xCluster replication

The combination of PITR and [xCluster replication](../../../explore/multi-region-deployments/asynchronous-replication-ysql/) is not fully tested, and is considered beta.

xCluster does not replicate any commands related to PITR. If you have two clusters with replication between them, enable PITR on both ends separately. To restore, the following is the recommended procedure:

1. Stop application workloads and make sure there are no active transactions.
1. Wait for replication to complete.
1. Restore to the same time on both clusters.
1. Resume the application workloads.

Tracking issue: [10820](https://github.com/yugabyte/yugabyte-db/issues/10820)

### Sequences

`CREATE SEQUENCE` and `DROP SEQUENCE` commands are not reverted by PITR. For example, if you restore to a point in time before a sequence was dropped, the sequence will not restored.

PITR support for sequences is available starting with version 2.14.

### Tablegroups

Using PITR with [tablegroups](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/ysql-tablegroups.md) is not currently supported.

Tracking issue: [11924](https://github.com/yugabyte/yugabyte-db/issues/11924)

### Global objects

PITR doesn't support global objects, such as [tablespaces](../../../explore/ysql-language-features/going-beyond-sql/tablespaces/), roles, and permissions, because they're not currently backed up by the distributed snapshots. If you alter or drop a global object, then try to restore to a point in time before the change, the object will _not_ be restored.

Tracking issue for YSQL tablespaces: [10257](https://github.com/yugabyte/yugabyte-db/issues/10257)

Tracking issue for YSQL roles and permissions: [10349](https://github.com/yugabyte/yugabyte-db/issues/10349)

Tracking issue for YCQL: [8453](https://github.com/yugabyte/yugabyte-db/issues/8453)

### YSQL system catalog upgrade

You can't use PITR to restore to a state before the most recent [YSQL system catalog upgrade](../../../admin/yb-admin/#upgrade-ysql-system-catalog). You can still use [distributed snapshots](../../../manage/backup-restore/snapshot-ysql/) to restore in this scenario.

Tracking issue: [13158](https://github.com/yugabyte/yugabyte-db/issues/13158)

This limitation applies only to YSQL databases. YCQL is not affected.

### Other limitations

* Using `TRUNCATE` command is not recommended for databases with a snapshot schedule. Restoring a database that was previously truncated can lead to an inconsistent state. Tracking issue: [7129](https://github.com/yugabyte/yugabyte-db/issues/7129).
* PITR works only with _in-cluster_ distributed snapshots. PITR support for off-cluster backups is under consideration for the future. Tracking issue: [8847](https://github.com/yugabyte/yugabyte-db/issues/8847).
* You can't modify a snapshot schedule once it's created. If you need to change the interval or the retention period, delete the snapshot and recreate it with the new parameters. Tracking issue: [8417](https://github.com/yugabyte/yugabyte-db/issues/8417).
