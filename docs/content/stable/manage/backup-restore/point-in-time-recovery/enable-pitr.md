---
title: Enable and disable point-in-time recovery
headerTitle: Enable and disable PITR
linkTitle: Enable and disable
description: Create and manage snapshot schedules to enable point-in-time recovery in YugabyteDB
menu:
  stable:
    identifier: pitr-enable
    parent: point-in-time-recovery
    weight: 10
type: docs
---

A snapshot schedule automatically takes periodic snapshots of a YSQL database or YCQL keyspace and retains them for a configured duration. Creating a schedule is what enables [Rewind to PIT](../rewind/) and [Clone to PIT](../clone/) for that database or keyspace. You cannot rewind unless you create a schedule first.

[Inspect at PIT](../inspect/) uses history retention flags and does not require a schedule. [Restore to PIT](../restore/) restores a specific snapshot and also does not require a schedule, though a schedule is one way to produce the snapshots you restore.

## Create a schedule

To create a schedule, use the [create_snapshot_schedule](../../../../admin/yb-admin/#create-snapshot-schedule) command with the following parameters:

- Interval between snapshots (in minutes).
- Total retention time (in minutes).
- The name of the database or keyspace.

For example, to create a schedule that produces a snapshot of a YSQL database once a day (every 1,440 minutes) and retains it for three days (4,320 minutes), execute the following command:

```sh
./bin/yb-admin --master_addresses <ip1:7100,ip2:7100,ip3:7100> create_snapshot_schedule 1440 4320 ysql.<database_name>
```

The equivalent command for a YCQL keyspace is the following:

```sh
./bin/yb-admin --master_addresses <ip1:7100,ip2:7100,ip3:7100> create_snapshot_schedule 1440 4320 <keyspace_name>
```

The following output is a unique ID of the newly created snapshot schedule:

```output.json
{
  "schedule_id": "6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256"
}
```

You can use this ID to [delete the schedule](#delete-a-schedule) or [rewind to a point in time](../rewind/).

## Delete a schedule

To delete a schedule and disable Rewind and Clone for that database or keyspace, use the [delete_snapshot_schedule](../../../../admin/yb-admin/#delete-snapshot-schedule) command with the ID of the schedule to delete:

```sh
./bin/yb-admin --master_addresses <ip1:7100,ip2:7100,ip3:7100> delete_snapshot_schedule 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256
```

## List schedules

To see a list of schedules that currently exist in the cluster, use the [list_snapshot_schedules](../../../../admin/yb-admin/#list-snapshot-schedules) command:

```sh
./bin/yb-admin --master_addresses <ip1:7100,ip2:7100,ip3:7100> list_snapshot_schedules
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

You can also use the same command to view information about a particular schedule by providing its ID:

```sh
./bin/yb-admin --master_addresses <ip1:7100,ip2:7100,ip3:7100> list_snapshot_schedules 6eaaa4fb-397f-41e2-a8fe-a93e0c9f5256
```

## Configuration details

By default, the history retention period is controlled by the [history retention interval flag](../../../../reference/configuration/yb-tserver/#timestamp-history-retention-interval-sec). This is a cluster-wide global flag that affects every YSQL database and YCQL keyspace, whether or not PITR is enabled.

When a snapshot schedule is configured for a particular database or keyspace, the per-database retention period is the maximum of the global history retention period and the snapshot interval specified for that schedule.

For example, if the global history retention period is 8 hours, but a schedule takes snapshots every 4 hours, then a snapshot taken at time t0 will have all data from (time t0 - 8h to time t0), even if that means two snapshots have overlapping and duplicate copies of the same detailed change data.
