---
title: Back up and restore data
headerTitle: Backup and restore
linkTitle: Backup and restore
description: Back up and restore YugabyteDB
image: fa-light fa-life-ring
headcontent: Create backups and restore your data
menu:
  stable:
    identifier: backup-restore
    parent: manage
    weight: 702
type: indexpage
---

Backup and restoration is the process of creating and storing copies of your data for protection against data loss. With a proper backup strategy, you can always restore your data to a most-recent known working state and minimize application downtime. This in turn guarantees business and application continuity.

Unlike traditional single-instance databases, YugabyteDB is designed for fault tolerance. By maintaining at least three copies of your data across multiple data regions or multiple clouds, it makes sure no losses occur if a single node or single data region becomes unavailable. Thus, with YugabyteDB, you would mainly use backups to:

- Recover from a user or software error, such as accidental table removal.
- Recover from a disaster scenario, like a full cluster failure or a simultaneous outage of multiple data regions. Even though such scenarios are extremely unlikely, it's still a best practice to maintain a way to recover from them.
- Maintain a remote copy of data, as required by data protection regulations.

## Best practices

- Don't perform cluster operations at the same time as your scheduled backup.
- Configure your maintenance window and backup schedule so that they do not conflict.
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
    icon="fa-light fa-file-import">}}

  {{<index/item
    title="Distributed snapshots"
    body="Back up and restore data using distributed snapshots."
    href="snapshot-ysql/"
    icon="fa-light fa-camera">}}

  {{<index/item
    title="Point-in-time recovery"
    body="Restore data to a particular point in time."
    href="point-in-time-recovery/"
    icon="fa-light fa-timeline-arrow">}}

{{</index/block>}}
