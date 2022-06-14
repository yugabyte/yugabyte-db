---
title: Point-in-time recovery
headerTitle: Point-in-time recovery
linkTitle: Point-in-time recovery
description: Restore data to a specific point in time in YugabyteDB
aliases:
  - /preview/manage/backup-restore/point-in-time-restore
  - /preview/manage/backup-restore/point-in-time-restore-ysql
  - /preview/manage/backup-restore/point-in-time-restore-ycql
menu:
  preview:
    identifier: point-in-time-recovery
    parent: backup-restore
    weight: 705
isTocNested: true
showAsideToc: true
---

_Point-in-time recovery_ (or PITR) in YugabyteDB is designed to provide an ability to recover from a user or software error, while minimizing recovery point objective (RPO), recovery time objective (RTO), and overall impact to the cluster.

PITR is particularly applicable in case of:

* DDL errors, such as an accidental table removal.
* DML errors, such as when an incorrect update statement is executed against one of the tables.

In such scenarios, you would typically know the time when the data was corrupted, and would want to restore to the closest possible uncorrupted state. With PITR, you can achieve that by providing a timestamp to restore to. You can specify the time with the precision of up to 1 microsecond, far more precision than is possible with the regular snapshots which are typically taken hourly or daily.

## How PITR works

PITR in YugabyteDB is based on a combination of the flashback capability and periodic [distributed snapshots](../snapshot-ysql).

Flashback is a feature that allows to rewind the data back in time. At any moment, YugabyteDB stores not only the latest state of the data, but also the recent history of changes. With flashback, you can rollback to any point in time in the history retention period. The history is also preserved when a snapshot is taken, which means that by creating snapshots periodically, you effectively increase the flashback retention.

For example, if your overall retention target for PITR is 3 days, you can use the following configuration:

* History retention interval is 24 hours.
* Snapshots are taken daily.
* Each snapshot is kept for 3 days.

{{< note title="History retention interval flag" >}}

By default, the history retention period is controlled by the [history retention interval flag](../../../reference/configuration/yb-tserver/#timestamp-history-retention-interval-sec). This flag is applied cluster-wide, to every YSQL database and YCQL keyspace.

However, when [PITR is enabled](#creating-a-schedule) for a database or a keyspace, YugabyteDB adjusts the history retention for that database/keyspace based on the interval between the snapshots. You're _not_ required to manually set the cluster-wide flag in order to use PITR.

{{< /note >}}

{{< note title="Space consumption" >}}

There are no technical limitations on the retention target. However, it's important to keep in mind that by increasing the number of snapshots stored, you also increase the amount of space required for the database. The actual overhead depends on the workload, so we recommend to estimate it by running tests based on your applications.

{{< /note >}}

The configuration above ensures that at any moment there is a continuous change history maintained for the last 3 days. When you trigger a restore, YugabyteDB will pick the closest snapshot to the timestamp you provide, and then use flashback in that snapshot.

Let's say the snapshots are taken daily at 11:00 PM, current time is 5:00 PM on April 14th, and you want to restore to 3:00 PM on April 12th. In this case, YugabyteDB:

1. Locates the snapshot taken on April 12th (which is the closest snapshot taken _after_ the restore time), and restores that snapshot.
1. Flashes back 8 hours to restore to the state at 3:00 PM (as opposed to 11:00 PM, which is when the snapshot was taken).

![Point-In-Time Recovery](/images/manage/backup-restore/pitr.png)

## Enable and disable PITR

YugabyteDB exposes the PITR functionality through a set of [snapshot schedule](../../../admin/yb-admin/#backup-and-snapshot-commands) CLI commands. A schedule is an entity that automatically manages periodic snapshots for a YSQL database or a YCQL keyspace, and enables PITR for the same database or keyspace.

{{< note >}}

Creating a snapshot schedule for a database or a keyspace effectively enables PITR for that database/keyspace. You can't recover to point in time unless you create a schedule.

{{< /note >}}

### Create a schedule

To create a schedule and enable PITR, use the [`create-snapshot-schedule`](../../../admin/yb-admin/#create-snapshot-schedule) command. This command takes the following parameters:

* Interval between snapshots (in minutes).
* Retention time for every snapshot (in minutes).
* The name of the database or keyspace.

Assuming the retention target is 3 days, you should create a schedule that creates a snapshot once a day (every 1,440 minutes), and retains it for 3 days (4,320 minutes):

```sh
$ ./bin/yb_admin create-snapshot-schedule 1440 4320 my_database
```

Once completed, the command will return and print out the unique ID of the newly created snapshot schedule:

```output.json
{
  "schedule_id": "6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256"
}
```

You can use this ID to [delete the schedule](#deleting-a-schedule) or [restore to a point in time](#restoring-to-a-point-in-time).

### Delete a schedule

To delete a schedule and disable PITR, use the [`delete-snapshot-schedule`](../../../admin/yb-admin/#delete-snapshot-schedule) command. It takes a single parameter: the ID of the schedule to be deleted.

```sh
$ ./bin/yb_admin delete-snapshot-schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256
```

### List existing schedules

When you want to see the list of schedules that currently exist in the cluster, use the [`list-snapshot-schedules`](../../../admin/yb-admin/#list-snapshot-schedules) command:

```sh
$ ./bin/yb_admin list-snapshot-schedules
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

You can also use the same command to view the information about a particular schedule by providing its ID as a parameter:

```sh
$ ./bin/yb_admin list-snapshot-schedules 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256
```

## Restore to a point in time

{{< warning title="Warning!" >}}

Before initiating restoration to a point in time, it's recommended that you stop all the application workloads. Transactions running concurrently with the restore operation can lead to data inconsistency.

This requirement will be removed in the future releases. Tracking issue: [12853](https://github.com/yugabyte/yugabyte-db/issues/12853)

{{< /warning >}}

If a database or a keyspace has an associated snapshot schedule, you can use that schedule to restore the database or keyspace to a particular point in time.

To restore, use the [`restore-snapshot-schedule`](../../../admin/yb-admin/#restore-snapshot-schedule) command. This command takes two parameters:

* The ID of the schedule.
* Target restore time.

For the second parameter, you have two options. You can either restore to an absolute time, providing a specific timestamp, or to a time that is relative to the current (for example, to 10 minutes ago from now).

{{< note title="YSQL index backfill" >}}

YugabyteDB supports [index backfill](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/online-index-backfill.md), which is the process of asynchronous population of a new index. The process runs in the background and can take significant amount of time, depending on the size of the data. If you restore to a point in time that is not long ago after an index creation, it is likely you will hit a state where the index is in the middle of the backfill process.

**Such indexes are not functional, as YugabyteDB ignores them during read operations. For YSQL databases, you need to drop these indexes and recreated them again to reinitiate the backfill process.**

This requirement will be removed in the future releases. Tracking issue: [12672](https://github.com/yugabyte/yugabyte-db/issues/12672)

Note that this affects only YSQL databases. In case of YCQL, index backfill is restarted automatically after the restore.

{{< /note >}}

### Restore to an absolute time

To restore to an absolute time you need to provide a timestamp you want to restore to. The following formats are supported:

* [Unix timestamp](https://www.unixtimestamp.com) in seconds, milliseconds or microseconds.
* [YSQL timestamp](../../../api/ysql/datatypes/type_datetime/).
* [YCQL timestamp](../../../api/ycql/type_datetime/#timestamp).

For example, the following command restores to 1:00 PM PDT on May 1st 2022 using a Unix timestamp:

```sh
$ ./bin/yb_admin restore-snapshot-schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256 \
                                           1651435200
```

The equivalent command that uses a YCQL timestamp is:

```sh
$ ./bin/yb_admin restore-snapshot-schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256 \
                                           2022-05-01 13:00-0700
```

### Restore to a relative time

Alternatively, you can restore to a time relative to the current moment, by specifying how much time back you would like to roll a database or keyspace back to.

For example, to restore to 5 minutes ago, run the following command:

```sh
$ ./bin/yb_admin restore-snapshot-schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256 \
                                           minus 5m
```

Or, to restore to 1 hour ago, use the following:

```sh
$ ./bin/yb_admin restore-snapshot-schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256 \
                                           minus 1h
```

For detailed information on the relative time formatting, refer to the [`restore-snapshot-schedule` reference](../../../admin/yb-admin/#restore-snapshot-schedule).

## Limitations

PITR functionality comes with several limitations, mainly related to it being used together with other features of YugabyteDB. Most of the limitations will be addressed in the future, please refer to corresponding tracking issues for more details.

###  CDC

Combination of PITR and [CDC](../../../explore/change-data-capture/) is currently not supported.

Tracking issue: [12773](https://github.com/yugabyte/yugabyte-db/issues/12773)

### xCluster replication

Combination of PITR and [xCluster replication](../../../explore/multi-region-deployments/asynchronous-replication-ysql/) is not fully tested and is considered beta.

xCluster does not replicate any commands related to PITR, so if you have two clusters with replication between them, you should enable PITR on both ends separately. In case of restore, the recommended procedure is the following:
1. Stop application workloads and make sure there are no active transactions.
1. Wait for replication to complete.
1. Restore to the same time on both clusters.
1. Resume the application workloads.

Tracking issue: [10820](https://github.com/yugabyte/yugabyte-db/issues/10820)

### Tablegroups

Combination of PITR and [tablegroups](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/ysql-tablegroups.md) is currently not supported. If you attempt to create a PITR schedule within a cluster with tablegroups, you will get an error. An attempt to create a tablegroup if a schedule exists on **any** of the databases will also end up in an error.

Tracking issue: [11924](https://github.com/yugabyte/yugabyte-db/issues/11924)

### Global objects

Global objects, such as [tablespaces](../../../explore/ysql-language-features/going-beyond-sql/tablespaces/), roles and permissions are not currently backed up by the distributed snapshots, and therefore are not supported by PITR. If you alter or drop a global object, and then try to restore to a point in time before the change, the object will **not** be restored.

Tracking issue for YSQL tablespaces: [10257](https://github.com/yugabyte/yugabyte-db/issues/10257)

Tracking issue for YSQL roles and permissions: [10349](https://github.com/yugabyte/yugabyte-db/issues/10349)

Tracking issue for YCQL: [8453](https://github.com/yugabyte/yugabyte-db/issues/8453)

{{< note title="Special case for tablespaces" >}}

Tablespaces are crucial for geo-partitioned deployments. Trying to restore a database that relies on a tablespace that had been removed can lead to unexpected behavior, so `DROP TABLESPACE` command is currently disallowed if a schedule exists on **any** of the databases within the cluster.

{{< /note >}}

### YSQL system catalog upgrade

PITR can't be used to restore to a state before the latest [YSQL system catalog upgrade](../../../admin/yb-admin/#upgrade-ysql-system-catalog). Trying to do so will produce an error. You can still use [distributed snapshots](../../../manage/backup-restore/snapshot-ysql/) to restore in such scenarios.

{{< note >}}

This limitation is only applicable to YSQL databases. YCQL is not affected.

{{< /note >}}

### Other restrictions

* `TRUNCATE` command is disallowed for a database with a snapshot schedule.
* A database can't be dropped if there is at least one schedule assigned to it. If you need to drop a database, you need to delete all its schedules first.
* PITR works **only** with in-cluster distributed snapshot. PITR support for off-cluster backups is considered for the future. Tracking issue: [8847](https://github.com/yugabyte/yugabyte-db/issues/8847).
* Snapshot schedules can't be modified once created. If you need to change the interval or the retention period, you should delete the snapshot and recreate it with new parameters. Tracking issue: [8417](https://github.com/yugabyte/yugabyte-db/issues/8417).
