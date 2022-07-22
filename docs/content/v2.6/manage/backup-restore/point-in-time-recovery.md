---
title: Point-in-Time Recovery
headerTitle: Point-in-time recovery
linkTitle: Point-in-time recovery
description: Restore data from a specific point in time in YugabyteDB
menu:
  v2.6:
    identifier: point-in-time-recovery
    parent: backup-restore
    weight: 704
type: docs
---

{{< warning title="YSQL support" >}}

YugabyteDB version 2.6 provides point-in-time recovery support only for YCQL keyspaces. Support for YSQL databases is provided since version 2.8.

{{< /warning >}}

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

By default, the history retention period is controlled by the [history retention interval flag](../../../reference/configuration/yb-tserver/#timestamp-history-retention-interval-sec) applied cluster-wide to every YCQL keyspace.

However, when [PITR is enabled](#create-a-schedule) for a keyspace, YugabyteDB adjusts the history retention for that keyspace based on the interval between the snapshots. You are not required to manually set the cluster-wide flag in order to use PITR.

There are no technical limitations on the retention target. However, when you increase the number of stored snapshots, you also increase the amount of space required for the keyspace. The actual overhead depends on the workload, therefore it is recommended to estimate it by running tests based on your applications.

The preceding sample configuration ensures that at any moment there is a continuous change history maintained for the last three days. When you trigger a restore, YugabyteDB selects the closest snapshot to the timestamp you provide, and then uses flashback in that snapshot.

For example, snapshots are taken daily at 11:00 PM, current time is 5:00 PM on April 14th, and you want to restore to 3:00 PM on April 12th. YugabyteDB performs the following:

1. Locates the snapshot taken on April 12th, which is the closest snapshot taken after the restore time, and restores that snapshot.
1. Flashes back 8 hours to restore to the state at 3:00 PM, as opposed to 11:00 PM, which is when the snapshot was taken.

![Point-In-Time Recovery](/images/manage/backup-restore/pitr.png)

## Enable and disable PITR

YugabyteDB exposes the PITR functionality through a set of [snapshot schedule](../../../admin/yb-admin/#backup-and-snapshot-commands) commands. A schedule is an entity that automatically manages periodic snapshots for a keyspace, and enables PITR for the same keyspace.

Creating a snapshot schedule for a keyspace effectively enables PITR for that keyspace. You cannot recover to point in time unless you create a schedule.

### Create a schedule

To create a schedule and enable PITR, use the [`create_snapshot_schedule`](../../../admin/yb-admin/#create-snapshot-schedule) command with the following parameters:

* Interval between snapshots (in minutes).
* Retention time for every snapshot (in minutes).
* The name of the keyspace.

Assuming the retention target is three days, you can execute the following command to create a schedule that produces a snapshot once a day (every 1,440 minutes) and retains it for three days (4,320 minutes):

```sh
./bin/yb-admin create_snapshot_schedule 1440 4320 <keyspace_name>
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
./bin/yb-admin delete_snapshot_schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256
```

### List schedules

To see a list of schedules that currently exist in the cluster, use the following [`list_snapshot_schedules`](../../../admin/yb-admin/#list-snapshot-schedules) command:

```sh
./bin/yb-admin list_snapshot_schedules
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
./bin/yb-admin list_snapshot_schedules 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256
```

## Restore to a point in time

{{< warning title="Stop workloads before restoring" >}}

Stop all the application workloads before you restore to a point in time. Transactions running concurrently with the restore operation can lead to data inconsistency.

This requirement will be removed in an upcoming release, and is tracked in issue [12853](https://github.com/yugabyte/yugabyte-db/issues/12853).

{{< /warning >}}

If a keyspace has an associated snapshot schedule, you can use that schedule to restore the keyspace to a particular point in time by using the [`restore_snapshot_schedule`](../../../admin/yb-admin/#restore-snapshot-schedule) command with the following parameters:

* The ID of the schedule.

* Target restore time, with the following two options:

  * Restore to an absolute time, providing a specific timestamp in one of the following formats:

    * [Unix timestamp](https://www.unixtimestamp.com) in seconds, milliseconds, or microseconds.
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

PITR functionality has several limitations, primarily related to interactions with other YugabyteDB features. Most of these limitations will be addressed in upcoming releases; refer to each limitation's corresponding tracking issue for details.

### Global objects

PITR doesn't support global objects, such as roles and permissions, because they're not currently backed up by the distributed snapshots. If you alter or drop a global object, then try to restore to a point in time before the change, the object will _not_ be restored.

Tracking issue: [8453](https://github.com/yugabyte/yugabyte-db/issues/8453)

### Other limitations

* PITR works only with _in-cluster_ distributed snapshots. PITR support for off-cluster backups is under consideration for the future. Tracking issue: [8847](https://github.com/yugabyte/yugabyte-db/issues/8847).
* You can't modify a snapshot schedule once it's created. If you need to change the interval or the retention period, delete the snapshot and recreate it with the new parameters. Tracking issue: [8417](https://github.com/yugabyte/yugabyte-db/issues/8417).
