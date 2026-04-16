---
title: Back up and restore data
headerTitle: Backup and restore
linkTitle: Backup and restore
description: Back up and restore YugabyteDB
headcontent: Create backups and restore your data
menu:
  v2.25:
    identifier: backup-restore
    parent: manage
    weight: 702
type: indexpage
---

{{< page-finder/head text="Back Up and Restore" subtle="across different products">}}
  {{< page-finder/list icon="/icons/database-hover.svg" text="YugabyteDB" current="" >}}
  {{< page-finder/list icon="/icons/server-hover.svg" text="YugabyteDB Anywhere" url="../../yugabyte-platform/back-up-restore-universes/" >}}
  {{< page-finder/list icon="/icons/cloud-hover.svg" text="YugabyteDB Aeon" url="/preview/yugabyte-cloud/cloud-clusters/backup-clusters/" >}}
{{< /page-finder/head >}}

Backup and restoration is the process of creating and storing copies of your data for protection against data loss. With a proper backup strategy, you can always restore your data to a most-recent known working state and minimize application downtime. This in turn guarantees business and application continuity.

Unlike traditional single-instance databases, YugabyteDB is designed for fault tolerance. By maintaining at least three copies of your data across multiple data regions or multiple clouds, it makes sure no losses occur if a single node or single data region becomes unavailable. Thus, with YugabyteDB, you would mainly use backups to:

- Recover from a user or software error, such as accidental table removal.
- Recover from a disaster scenario, like a full cluster failure or a simultaneous outage of multiple data regions. Even though such scenarios are extremely unlikely, it's still a best practice to maintain a way to recover from them.
- Maintain a remote copy of data, as required by data protection regulations.

## Best practices

- Don't perform cluster operations at the same time as your scheduled backup.
- Configure your maintenance window and backup schedule so that they do not conflict.
- Perform full backups before performing a large operation, such as a DDL change.
- Performing a backup or restore incurs a load on the cluster. Perform backup operations when the cluster isn't experiencing heavy traffic. Backing up during times of heavy traffic can temporarily degrade application performance and increase the length of time of the backup.
- Avoid running a backup during or before a scheduled maintenance.

{{< warning title="Backups and high DDL activity" >}}
In some circumstances, a backup can fail during high DDL activity. Avoid performing major DDL operations during scheduled backups or while a backup is in progress.
{{< /warning >}}

{{<index/block>}}

  {{<index/item
    title="Export and import"
    body="Export and import data using SQL or CQL scripts."
    href="export-import-data/"
    icon="fa-thin fa-file-import">}}

  {{<index/item
    title="Distributed snapshots"
    body="Back up and restore data using distributed snapshots."
    href="snapshot-ysql/"
    icon="fa-thin fa-camera">}}

  {{<index/item
    title="Point-in-time recovery"
    body="Restore data to a particular point in time."
    href="point-in-time-recovery/"
    icon="fa-thin fa-timeline-arrow">}}

  {{<index/item
    title="Instant database cloning"
    body="Clone a database for data recovery, development, and testing."
    href="instant-db-cloning/"
    icon="fa-thin fa-clone">}}

  {{<index/item
    title="Time travel query"
    body="Query data as at a specific point in time."
    href="time-travel-query/"
    icon="fa-thin fa-police-box">}}

{{</index/block>}}
