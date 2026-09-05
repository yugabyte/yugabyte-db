---
title: Point-in-time recovery
headerTitle: Point-in-time recovery
linkTitle: Point-in-time recovery
description: Recover from logical SQL or CQL errors using Inspect, Clone, Rewind, and Restore to a point in time
headcontent: Recover from logical errors quickly with little or no data movement
aliases:
  - /stable/manage/backup-restore/point-in-time-restore
  - /stable/manage/backup-restore/point-in-time-restore-ycql
  - /stable/yugabyte-platform/back-up-restore-universes/point-in-time
menu:
  stable:
    identifier: point-in-time-recovery
    parent: backup-restore
    weight: 705
type: indexpage
showRightNav: true
---

{{< page-finder/head text="Point-in-time recovery" subtle="across different products">}}
  {{< page-finder/list icon="/icons/database-hover.svg" text="YugabyteDB" current="" >}}
  {{< page-finder/list icon="/icons/server-hover.svg" text="YugabyteDB Anywhere" url="../../../yugabyte-platform/back-up-restore-universes/pitr/" >}}
  {{< page-finder/list icon="/icons/cloud-hover.svg" text="YugabyteDB Aeon" url="/stable/yugabyte-cloud/cloud-clusters/aeon-pitr/" >}}
{{< /page-finder/head >}}

Point-in-time (PIT) recovery in YugabyteDB is a set of capabilities for recovering from human or logical errors at the SQL or CQL level. For example, a mistyped `UPDATE`, an accidental `DROP TABLE`, or a bad application write.

These scenarios are different from hardware failure or disk corruption. Failed or corrupted disks typically require a full [backup and restore](../), which moves large amounts of data and can involve significant downtime. PIT recovery instead aims to get you back to a known-good state quickly, often with little or no data movement and without taking the cluster offline for a bulk restore.

You recover to a user-specified moment (up to microsecond precision) inside a configured retention window. To use most of these capabilities, you first [enable PITR](enable-pitr/) by creating a snapshot schedule for the database or keyspace.

## The PIT recovery family

YugabyteDB provides four complementary ways to work with a point in time:

| Capability | What it does | Best when |
| :--- | :--- | :--- |
| [Inspect at PIT](inspect/) | Query the database as it existed at an earlier time (read-only). Also referred to as time travel queries. | You need to find when or what went wrong, or recover a small amount of data surgically on the production database. |
| [Clone to PIT](clone/) | Create a fast, lightweight, writable copy (or branch) of the database as of a point in time on the same cluster. Also referred to as database branching. | Recent writes must be preserved; you can perform forensic search and extract/merge on the original cluster. |
| [Rewind to PIT](rewind/) | Rewind the original database to an earlier point in time. Intervening writes are discarded. | There were no important writes after the error, or those writes can be discarded or replayed from an external log. |
| [Restore to PIT](restore/) | Restore from a snapshot or backup to a chosen point in time, on the original or an alternate cluster. | Policy requires recovery off the production cluster, or you need a longer retention window on cheaper backup storage. |

### Choosing an approach

- Start with [Inspect at PIT](inspect/) when you need to determine *when* the error occurred or *what* data changed.
- Use [Rewind to PIT](rewind/) when intervening writes can be discarded (or replayed from an external application log).
- Use [Clone to PIT](clone/) when intervening writes must be preserved and forensic recovery is allowed on the production cluster.
- Use [Restore to PIT](restore/) when recovery must happen on an alternate cluster, or you need a longer retention window from backup storage. On a manually managed cluster this is an advanced workflow; [YugabyteDB Anywhere](../../../yugabyte-platform/back-up-restore-universes/restore-universe-data/) is the recommended path.

Use the following comparison when deciding:

|      | Inspect at PIT | Clone to PIT | Rewind to PIT | Restore to PIT |
| :--- | :------------- | :----------- | :------------ | :------------- |
| Target cluster | Original | Original | Original | Original or alternate |
| Database affected | Original (read-only view) | New cloned database | Original database | Restored database |
| Crosses DDL boundaries | No | Yes | Yes | No |
| Newest recoverable time | Seconds ago | Seconds ago | Seconds ago | Time of last [snapshot/backup](../snapshot-ysql/) |
| Typical retention | Hours (primary storage) | Hours to days (primary storage) | Hours to days (primary storage) | Hours to months (backup storage) |
| APIs | YSQL | YSQL and YCQL | YSQL and YCQL | YSQL and YCQL |

### Availability

PITR features are available in YugabyteDB Anywhere and Aeon as follows:

|      | Inspect at PIT | Clone to PIT | Rewind to PIT | Restore to PIT |
| :--- | :------------- | :----------- | :------------ | :------------- |
| YugabyteDB | SQL | SQL and yb-admin | yb-admin | Advanced (yb-admin) |
| YugabyteDB Anywhere| SQL | Not in UI | Yes | Yes |
| YugabyteDB Aeon| SQL | Yes | No | No |

#### YugabyteDB Anywhere

YugabyteDB Anywhere supports the following PITR features:

- [Inspect at PIT](inspect/). Supported in SQL. You can use Inspect on YugabyteDB Anywhere-deployed universes as you would on any YugabyteDB universe.
- [Clone to PIT](clone/). You can clone a PITR-enabled database using SQL (YSQL) or yb-admin (YCQL), but you cannot manage clones in the UI. For recovery, Rewind and Restore are recommended.
- [Rewind to PIT](../../../yugabyte-platform/back-up-restore-universes/pitr/). Enable and manage Rewind using the YugabyteDB Anywhere UI.
- [Restore to PIT](../../../yugabyte-platform/back-up-restore-universes/restore-universe-data/). Enable and manage Restore using the YugabyteDB Anywhere UI.

{{< warning title="Do not mix yb-admin and the YugabyteDB Anywhere UI" >}}

A database or keyspace can have at most one snapshot schedule. On a universe managed by YugabyteDB Anywhere, manage PITR strictly using YugabyteDB Anywhere. Using yb-admin and the UI together to manage snapshot schedules can cause conflicts. Changes you make using yb-admin are not reflected in YugabyteDB Anywhere.

{{< /warning >}}

#### YugabyteDB Aeon

YugabyteDB Aeon supports the following PITR features:

- [Inspect at PIT](inspect/). Supported in SQL. YugabyteDB Aeon clusters use the default history retention interval of 15 minutes.
- [Clone to PIT](../../../yugabyte-cloud/cloud-clusters/aeon-pitr/). Enable and manage Clone using the YugabyteDB Aeon UI.

## How it works

PIT capabilities rely on retained change history, typically provided by:

1. **Flashback / history retention**: YugabyteDB retains recent versions of data for a configurable period so the database can be read or rewound to any microsecond in that window. The default history retention is controlled by the [history retention interval flag](../../../reference/configuration/yb-tserver/#timestamp-history-retention-interval-sec).

1. **Periodic distributed snapshots**: [Distributed snapshots](../snapshot-ysql/) capture a lightweight copy of database data files. A [snapshot schedule](enable-pitr/) takes snapshots periodically and retains them for a configured duration, extending the continuous history window beyond a single flashback interval.

For example, if your overall retention target is three days, you can take snapshots daily, and retain each for three days. That configuration keeps a continuous change history for the last three days. When you rewind or restore to a point in time, YugabyteDB selects the closest suitable snapshot and uses flashback in that snapshot.

![Point-In-Time Recovery](/images/manage/backup-restore/pitr.png)

### Operational considerations

Enabling PITR impacts both disk consumption and performance. Keep in mind the following:

- Retaining more snapshots, or retaining snapshots for longer durations, increases storage consumption but has no impact on database performance. The actual overhead depends on the workload; estimate it by running tests based on your applications.
- Specifying a lower snapshot interval (particularly below 24 hours) can allow the database to reduce its internal history retention period. That can improve performance by allowing more frequent compaction and reducing DocDB scan times.

When PITR is enabled for a database or keyspace, the per-database retention period is the maximum of the global history retention period and the snapshot interval specified for that schedule.

## Learn more

{{<index/block>}}

  {{<index/item
    title="Enable and disable PITR"
    body="Create and manage snapshot schedules for a database or keyspace."
    href="enable-pitr/"
    icon="fa-thin fa-toggle-on">}}

{{</index/block>}}
{{<index/block>}}

  {{<index/item
    title="Inspect at PIT"
    body="Query data as it existed at a specific point in time."
    href="inspect/"
    icon="fa-thin fa-police-box">}}

  {{<index/item
    title="Clone to PIT"
    body="Create a lightweight writable clone as of a point in time."
    href="clone/"
    icon="fa-thin fa-clone">}}

  {{<index/item
    title="Rewind to PIT"
    body="Rewind a database to an earlier point in time."
    href="rewind/"
    icon="fa-thin fa-clock-rotate-left">}}

  {{<index/item
    title="Restore to PIT"
    body="Restore a snapshot or backup to a chosen point in time."
    href="restore/"
    icon="fa-thin fa-box-open">}}

{{</index/block>}}
