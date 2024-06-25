---
title: Point-in-time recovery
headerTitle: Point-in-time recovery
linkTitle: Point-in-time recovery
description: Restore data to a specific point in time in YugabyteDB
menu:
  stable:
    identifier: point-in-time-recovery
    parent: backup-restore
    weight: 705
type: docs
---

Point-in-time recovery (PITR) in YugabyteDB enables recovery from a user or software error, while minimizing recovery point objective (RPO), recovery time objective (RTO), and overall impact on the cluster.

PITR is particularly applicable to the following:

- DDL errors, such as an accidental table removal.
- DML errors, such as execution of an incorrect update statement against one of the tables.

Typically, you know when the data was corrupted and want to restore to the closest possible uncorrupted state. With PITR, you can achieve that by providing a timestamp to which to restore. You can specify the time with the precision of up to 1 microsecond, far more precision than is possible with the regular snapshots that are typically taken hourly or daily.

## How it works

PITR in YugabyteDB is based on a combination of the following:

1. Flashback

    Flashback provides a way to rewind a database back to any microsecond in time over a short history retention period. Flashback is automatically and internally managed by YugabyteDB for performance purposes. The history retention period defaults to 24 hours and may be reduced to improve DB performance.

1. Periodic distributed snapshots

    [Distributed snapshots](../snapshot-ysql) capture a lightweight zero-cost copy of database data files, including all detailed data changes, for the specified retention period. By creating and saving snapshots periodically, you effectively create a total PITR history, which is the combination of all of the individual snapshots.

For example, if your overall retention target for PITR is three days, you can specify the following configuration:

- Take snapshots daily.
- Retain each snapshot for three days.

This configuration ensures that at any moment there is a continuous change history maintained for the last three days. When you trigger a point-in-time restore, YugabyteDB selects the closest snapshot to the timestamp you provide, and then uses flashback in that snapshot.

For example, suppose snapshots are taken daily at 11:00 PM, the current time is 5:00 PM on April 14th, and you want to restore to 3:00 PM on April 12th. YugabyteDB does the following:

1. Locates the snapshot taken on April 12th, which is the closest snapshot taken after the restore time, and restores that snapshot.
1. Flashes back 8 hours to restore to the state at 3:00 PM (as opposed to 11:00 PM, when the snapshot was taken).

![Point-In-Time Recovery](/images/manage/backup-restore/pitr.png)

### Operational considerations

Enabling PITR impacts both disk consumption and performance. Keep in mind the following:

- Retaining more snapshots, or retaining snapshots for longer durations, increases storage consumption but has no impact on database performance. The actual storage consumption overhead depends on the workload, therefore it is recommended to estimate it by running tests based on your applications.
- Specifying a lower snapshot interval (particularly below 24 hours) can permit the DB to reduce its internal history retention period. This can improve database performance by allowing more frequent compactions to occur, and thus reduce DocDB scan times to retrieve a given record.

### Configuration details

By default, the history retention period is controlled by the [history retention interval flag](../../../reference/configuration/yb-tserver/#timestamp-history-retention-interval-sec). This is a cluster-wide global flag that affects every YSQL database and YCQL keyspace while PITR is enabled.

When [PITR is enabled](#create-a-schedule) for a particular database or keyspace, the per-database retention period is the maximum of the global history retention period and the snapshot interval specified when PITR is configured for the database or keyspace.

For example, if the global history retention period is 8 hours, but PITR is configured for a particular database to take snapshots every 4 hours, then a snapshot taken at time t0 will have all data from (time t0 - 8h to time t0), even if that means 2 snapshots have overlapping and duplicate copies of the same detailed change data.

## Enable and disable PITR

YugabyteDB exposes the PITR functionality through a set of [snapshot schedule](../../../admin/yb-admin/#backup-and-snapshot-commands) commands. A schedule is an entity that automatically manages periodic snapshots for a YSQL database or a YCQL keyspace, and enables PITR for the same database or keyspace.

Creating a snapshot schedule for a database or a keyspace effectively enables PITR for that database or keyspace. You cannot recover to point in time unless you create a schedule.

### Create a schedule

To create a schedule and enable PITR, use the [`create_snapshot_schedule`](../../../admin/yb-admin/#create-snapshot-schedule) command with the following parameters:

- Interval between snapshots (in minutes).
- Total retention time (in minutes).
- The name of the database or keyspace.

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

If a database or a keyspace has an associated snapshot schedule, you can use that schedule to restore the database or keyspace to a particular point in time by using the [`restore_snapshot_schedule`](../../../admin/yb-admin/#restore-snapshot-schedule) command with the following parameters:

- The ID of the schedule.

- Target restore time, with the following two options:

  - Restore to an absolute time, providing a specific timestamp in one of the following formats:

    - [Unix timestamp](https://www.unixtimestamp.com) in microseconds.
    - [YSQL timestamp](../../../api/ysql/datatypes/type_datetime/).
    - [YCQL timestamp](../../../api/ycql/type_datetime/#timestamp).

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

  - Restore to a time that is relative to the current (for example, to 10 minutes ago from now) by specifying how much time back you would like to roll a database or keyspace.

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

**YugabyteDB ignores these partly-backfilled indexes during read operations. To make sure the indexes are properly used, you need to drop and create them again to re-initiate the backfill process.** Run the following query to get a list of indexes that need to be recreated:

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

### CDC

Using PITR and [CDC](../../../explore/change-data-capture/) together is currently not supported.

Tracking issue: [12773](https://github.com/yugabyte/yugabyte-db/issues/12773)

### xCluster replication

xCluster does not replicate any commands related to PITR. If you have two clusters with replication between them, enable PITR on both ends independently. You can perform a restore using the following recommended procedure:

1. Stop application workloads and make sure there are no active transactions.
1. Wait for replication to complete.
1. Delete xCluster replication from both clusters.
1. Restore both clusters to the exact same time.
1. Re-establish xCluster replication.
1. Resume the application workloads.

### Global objects

PITR doesn't support global objects, such as [tablespaces](../../../explore/going-beyond-sql/tablespaces/), roles, and permissions, because they're not currently backed up by the distributed snapshots. If you alter or drop a global object, then try to restore to a point in time before the change, the object will _not_ be restored.

Tracking issue for YSQL tablespaces: [10257](https://github.com/yugabyte/yugabyte-db/issues/10257)

Tracking issue for YSQL roles and permissions: [10349](https://github.com/yugabyte/yugabyte-db/issues/10349)

Tracking issue for YCQL: [8453](https://github.com/yugabyte/yugabyte-db/issues/8453)

{{< note title="Special case for tablespaces" >}}

Tablespaces are crucial for geo-partitioned deployments. Trying to restore a database that relies on a removed tablespace will lead to unexpected behavior, so the `DROP TABLESPACE` command is currently disallowed if a schedule exists on _any_ of the databases in the cluster.

{{< /note >}}

### YSQL system catalog upgrade

You can't use PITR to restore to a state before the most recent [YSQL system catalog upgrade](../../../admin/yb-admin/#upgrade-ysql-system-catalog). Trying to do so will produce an error. You can still use [distributed snapshots](../../../manage/backup-restore/snapshot-ysql/) to restore in this scenario.

Tracking issue: [13158](https://github.com/yugabyte/yugabyte-db/issues/13158)

This limitation applies only to YSQL databases. YCQL is not affected.

### YugabyteDB Anywhere

YugabyteDB Anywhere [supports PITR](../../../yugabyte-platform/back-up-restore-universes/pitr/). However, you must initiate and manage PITR using the YugabyteDB Anywhere UI. If you use the yb-admin CLI to make changes to the PITR configuration of a universe managed by YugabyteDB Anywhere, including creating schedules and snapshots, your changes are not reflected in YugabyteDB Anywhere.

### Other limitations

- PITR works only with _in-cluster_ distributed snapshots. PITR support for off-cluster backups is under consideration for the future. Tracking issue: [8847](https://github.com/yugabyte/yugabyte-db/issues/8847).
- You can't modify a snapshot schedule once it's created. If you need to change the interval or the retention period, delete the snapshot and recreate it with the new parameters. Tracking issue: [8417](https://github.com/yugabyte/yugabyte-db/issues/8417).
- Issuing DDLs against a database while it is being restored is not recommended.
