---
title: Point-in-Time Recovery
headerTitle: Point-in-time recovery
linkTitle: Point-in-time recovery
description: Restore data from a specific point in time in YugabyteDB
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

_Point-in-time recovery_ (or PITR) in YugabyteDB is designed to provide an ability to recover from a user or software error, while minimizing RPO, RTO and overall impact of the cluster.

PITR is particularly useful in case of:
* DDL errors, like an accidental table removal
* DML errors, like when an incorrect update statement is executed against one of the tables

In such scenarios, you would typically know the time when the data was corrupted, and would want to restore to the closest possible uncorrupted state. With PITR, you can achieve that by providing a timestamp to restore to. You can specify the time with the precision of up to 1 microsecond - something that is not possible with the regular snapshots which are typically taken hourly or daily.

## How Does PITR Work?

PITR in YugabyteDB is based on a combination of the flashback capability and periodic [distributed snapshots](snapshot-ysql.md).

Flashback is a feature that allows to rewind the data back in time. At any moment, YugabyteDB stores not only the latest state of the data, but also the recent history of changes. Time period the history is maintained for is customizable and can be set via the [history retention interval flag](../../../reference/configuration/yb-tserver/#timestamp-history-retention-interval-sec). With flashback, you can rollback to any point in time within this interval. The change history is also preserved when a snapshot is taken, which means that by creating snapshots periodically, you effectively increase the flashback retention interval.

For example, if your overall retention target for PITR is 7 days, you can use the following configuration:
1. History retention interval is set to 25 hours (the corresponding flag value is 90,000 seconds).
2. Snapshots are taken every 24 hours.
3. The retention period for every snapshot is 7 days.

{{< note title="Note on the history retention interval" >}}

When configuring PITR, it is important to make sure the change history is continuous. To make sure that's the case, it is recommended that the history retention interval is slightly larger than the interval between snapshots. For example, if snapshots are taken daily (every 24 hours), the history retention interval should be set to 25 hours. This creates an overlap that guarantees continuity.

{{< /note >}}

{{< note title="Note on space consumption" >}}

There are no technical limitations on the retention target. However, it's important to keep in mind that by increasing the history retention interval and the number of snapshots stored, you also increase the amount of space required for the database. The actual overhead depends on the workload, so we recommend to estimate it by running tests based on your applications.

{{< /note >}}

The configuration above ensures that at any moment there is a continuous change history maintained for the last 7 days. When you want to restore, YugabyteDB will pick the closest snapshot to the timestamp you provide, and then use flashback within that snapshot.

Let's say the snapshots are taken daily at 11:00PM, current time is 5:00PM on April 14th, and you want to restore to 3:00PM on April 12th. In this case, YugabyteDB will:
1. Locate the snapshot taken on April 12th (the closest snapshot taken after the restore time).
2. Restore to that snapshot.
3. Rewind back 8 hours to rollback to the state at 3:00PM (as opposed to 11:00PM which is when the snapshot was taken).

[PIC?]

## Enabling and Disabling PITR

YugabyteDB exposes the PITR functionality through a set of [snapshot schedule](../../../admin/yb-admin/#backup-and-snapshot-commands) CLI commands. A schedule automatically manages periodic snapshots for a YSQL database or a YCQL keyspace, and enables PITR for the same database/keyspace.

{{< note >}}

You're required to create a snapshot schedule for any database/keyspace you want to have PITR enabled for.

{{< /note >}}

### Creating a Schedule

To create a schedule and enable PITR, use the [`create-snapshot-schedule`](../../../admin/yb-admin/#create-snapshot-schedule) command. It takes three parameters:
1. Interval between snapshots (in minutes)
2. Retention time for every snapshot (in minutes)
3. The name of the database or keyspace

Assuming that the history retention interval is set to 25 hours, and the retention target is 7 days, you should create a schedule that creates a snapshot once a day (every 1,440 minutes), and retains it for 7 days (10,080 minutes):

```sh
$ ./bin/yb_admin create-snapshot-schedule 1440 10080 my_database
```

Once completed, the command will return and print out the unique ID of the newly created snapshot schedule:

```sh
{
  "schedule_id": "6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256"
}
```

You can use this ID to [delete the schedule](LINK) or to [restore to a point in time](LINK).

### Deleting a Schedule

To delete a schedule and disable PITR, use the [`delete-snapshot-schedule`](../../../admin/yb-admin/#delete-snapshot-schedule) command. It takes a single parameter: the ID of the schedule to be deleted.

```sh
$ ./bin/yb_admin delete-snapshot-schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256
```

### Listing Existing Schedules

To print out the list of schedules that currently exist in the cluster, use the [`list-snapshot-schedules`](../../../admin/yb-admin/#delete-snapshot-schedules) command:

```sh
$ ./bin/yb_admin list-snapshot-schedules
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

## Restoring to a Point in Time

If a database or a keyspace has an associated snapshot schedule, you can use that schedule to restore the database/keyspace to a point in time.

To restore, use the [`restore-snapshot-schedule`](../../../admin/yb-admin/#restore-snapshot-schedule) command. It takes two parameters:
1. The ID of the schedule.
2. Target restore time.

For the second parameter, you have two options. You can either restore to an absolute time, providing a specific timestamp, or to a relative time (for example, to 10 minutes ago from now).

### Restoring to an Absolute Time

TBD

### Restoring to a Relative Time

TBD

## Limitations

This feature is in active development. YSQL and YCQL support different features, as detailed in the sections that follow.

### YSQL limitations

* For Sequences, restoring to a state before the sequence table was created/dropped doesn't work. This is being tracked in [issue 10249](https://github.com/yugabyte/yugabyte-db/issues/10249).

* Colocated Tables aren't supported and databases with colocated tables cannot be restored to a previous point in time. Tracked in [issue 8259](https://github.com/yugabyte/yugabyte-db/issues/8259).

* Cluster-wide changes such as roles and permissions, tablespaces, etc. aren't supported. Please note however that database-level operations such as changing ownership of a table of a database, row-level security, etc. can be restored as their scope is not cluster-wide. Tablespaces are tracked in [issue 10257](https://github.com/yugabyte/yugabyte-db/issues/10257) while roles and privileges are tracked in [issue 10349](https://github.com/yugabyte/yugabyte-db/issues/10349).

* Support for Triggers and Stored Procedures is to be investigated. Tracked in [issue 10350](https://github.com/yugabyte/yugabyte-db/issues/10350).

* In case of software upgrades/downgrades, we don't support restoring back in time to the previous version.

### YCQL limitations

* Support for YCQL roles and permissions is yet to be added. Tracked in [issue 8453](https://github.com/yugabyte/yugabyte-db/issues/8453).

### Common limitations

* Currently, we don't support some aspects of PITR in conjunction with xCluster replication. It is being tracked in [issue 10820](https://github.com/yugabyte/yugabyte-db/issues/10820).

* TRUNCATE TABLE is a limitation tracked in [issue 7130](https://github.com/yugabyte/yugabyte-db/issues/7130).

* We don't support DDL restores to a previous point in time using external backups. This is being tracked in [issue 8847](https://github.com/yugabyte/yugabyte-db/issues/8847).

Development for this feature is tracked in [issue 7120](https://github.com/yugabyte/yugabyte-db/issues/7120).
