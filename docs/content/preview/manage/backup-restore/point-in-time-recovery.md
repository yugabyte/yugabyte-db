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

Point-in-time recovery (PITR) in YugabyteDB enables recovery from a user or software error, while minimizing recovery point objective (RPO), recovery time objective (RTO), and overall impact on the cluster.

PITR is particularly applicable to the following:

* DDL errors, such as an accidental table removal.
* DML errors, such as execution of an incorrect update statement against one of the tables.

Typically, you know when the data was corrupted and would want to restore to the closest possible uncorrupted state. With PITR, you can achieve that by providing a timestamp to which to restore. You can specify the time with the precision of up to 1 microsecond, far more precision than is possible with the regular snapshots that are typically taken hourly or daily.

PITR in YugabyteDB is based on a combination of the flashback capability and periodic [distributed snapshots](../snapshot-ysql).

Flashback provides means to rewind the data back in time. At any moment, YugabyteDB stores not only the latest state of the data, but also the recent history of changes. With flashback, you can rollback to any point in time in the history retention period. The history is also preserved when a snapshot is taken, which means that by creating snapshots periodically, you effectively increase the flashback retention.

For example, if your overall retention target for PITR is three days, you can use the following configuration:

* History retention interval is 24 hours.
* Snapshots are taken daily.
* Each snapshot is kept for three days.

By default, the history retention period is controlled by the [history retention interval flag ](../../../reference/configuration/yb-tserver/#timestamp-history-retention-interval-sec)applied cluster-wide to every YSQL database and YCQL keyspace.

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
./bin/yb-admin create_snapshot_schedule 1440 4320 <database_name>
```

The following output is a unique ID of the newly-created snapshot schedule:

```json
{
  "schedule_id": "6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256"
}
```

You can use this ID to [delete the schedule](#deleting-a-schedule) or [restore to a point in time](#restoring-to-a-point-in-time).

### Delete a schedule

To delete a schedule and disable PITR, use the following [`delete_snapshot_schedule`](../../../admin/yb-admin/#delete-snapshot-schedule) command that takes the ID of the schedule to be deleted as a parameter:

```sh
./bin/yb-admin delete_snapshot_schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256
```

### List schedules

To see a list of schedules that currently exist in the cluster, use the following [`list_snapshot_schedules`](../../../admin/yb-admin/#list-snapshot-schedules) command:

```sh
./bin/yb-admin list_snapshot_schedules
```

```json
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
./bin/yb-admin list_snapshot_schedules 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256
```

## Restore to a point in time

If a database or a keyspace has an associated snapshot schedule, you can use that schedule to restore the database or keyspace to a particular point in time by using the [`restore_snapshot_schedule`](../../../admin/yb-admin/#restore-snapshot-schedule) command with the following parameters:

* The ID of the schedule.

* Target restore time, with the following two options: 

  * Restore to an absolute time, providing a specific timestamp in one of the following formats:

    * [Unix timestamp](https://www.unixtimestamp.com) in seconds, milliseconds, or microseconds.
    * [YSQL timestamp](../../../api/ysql/datatypes/type_datetime/).
    * [YCQL timestamp](../../../api/ycql/type_datetime/#timestamp).

    For example, the following command restores to 1:00 PM PDT on May 1st 2022 using a Unix timestamp:

    ```sh
    ./bin/yb-admin restore_snapshot_schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256 \
                                             1651435200
    ```

    The following is an equivalent command that uses a YCQL timestamp:

    ```sh
    ./bin/yb-admin restore_snapshot_schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256 \
                                             2022-05-01 13:00-0700
    ```

  * Restore to a time that is relative to the current (for example, to 10 minutes ago from now) by specifying how much time back you would like to roll a database or keyspace.

    For example, to restore to 5 minutes ago, run the following command:

    ```sh
    ./bin/yb-admin restore_snapshot_schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256 \
                                             minus 5m
    ```

    Or, to restore to 1 hour ago, use the following:

    ```sh
    ./bin/yb-admin restore_snapshot_schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256 \
                                             minus 1h
    ```

    For detailed information on the relative time formatting, refer to the [`restore_snapshot_schedule` reference](../../../admin/yb-admin/#restore-snapshot-schedule).

## Limitations

PITR is currently in active development, with different levels of support in YSQL and YCQL, as summarized in the following table.

| Limitation                                                   | Applicable to | Tracking number                                              |
| ------------------------------------------------------------ | ------------- | ------------------------------------------------------------ |
| For sequences, restoring to a state before the sequence table was created or dropped does not work. | YSQL          | [Issue 10249](https://github.com/yugabyte/yugabyte-db/issues/10249) |
| No support for colocated tables and databases with colocated tables cannot be restored to a previous point in time. | YSQL          | [Issue 8259](https://github.com/yugabyte/yugabyte-db/issues/8259) |
| No support for cluster-wide changes, such as roles and permissions, tablespaces, and so on. | YSQL          | [Issue 10257](https://github.com/yugabyte/yugabyte-db/issues/10257) <br>and<br> [Issue 10349](https://github.com/yugabyte/yugabyte-db/issues/10349) |
| No support for triggers and stored procedures.               | YSQL          | [Issue 10350](https://github.com/yugabyte/yugabyte-db/issues/10350) |
| During software upgrades and downgrades, restoring back in time to the previous version is not supported | YSQL          |                                                              |
| No support for YCQL roles and permissions.                   | YCQL          | [Issue 8453](https://github.com/yugabyte/yugabyte-db/issues/8453) |
| No support for certain aspects of PITR in conjunction with asynchronous replication. | YSQL and YCQL | [Issue 10820](https://github.com/yugabyte/yugabyte-db/issues/10820) |
| No support for `TRUNCATE TABLE`.                             | YSQL and YCQL | [Issue 7130](https://github.com/yugabyte/yugabyte-db/issues/7130) |
| No support for DDL restores to a previous point in time using external backups. | YSQL and YCQL | [Issue 8847](https://github.com/yugabyte/yugabyte-db/issues/8847) |

Overall development for PITR is tracked in [issue 7120](https://github.com/yugabyte/yugabyte-db/issues/7120).