---
title: Back up and restore universes
headerTitle: Back up and restore universes
linkTitle: Back up universes
description: Use YugabyteDB Anywhere to back up and restore YugabyteDB universe data.
image: fa-light fa-life-ring
headcontent: Use YugabyteDB Anywhere to back up and restore YugabyteDB universes and data
menu:
  stable_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: back-up-restore-universes
weight: 650
type: indexpage
---

You can use YugabyteDB to schedule and manage backups of your universe data. This includes the following features:

- On-demand [backup](back-up-universe-data/) and [restore](restore-universe-data/).
- [Scheduled backups](schedule-data-backups/). Schedule backups at regular intervals, along with retention periods.
- [Incremental backups](back-up-universe-data/#create-incremental-backups). Create a schedule to take full backups periodically and incremental backups between those full backups.
- [Configurable performance parameters](back-up-universe-data/#configure-backup-performance-parameters). Tune parallelization and buffers for faster backup and restore performance. In most cases, this results in 5x or more speed improvements in backups and restores.
- [Point-in-time recovery](pitr/). Recover universe data from a specific point in time.
- [Flexible storage](configure-backup-storage/). Store backups in the cloud or in your data center.
- [Disaster recovery](disaster-recovery/). Failover to an asynchronously replicated universe in case of unplanned outages.

{{< note title="Note" >}}
Configurable performance parameters and incremental backups are mediated using the yb-controller process, which is only available in YugabyteDB Anywhere v2.16 or later for universes with YugabyteDB version 2.16 or later.
{{< /note >}}

## Best practices

- Don't perform cluster operations at the same time as your scheduled backup.
- Configure your maintenance window and backup schedule so that they do not conflict.
- Performing a backup or restore incurs a load on the cluster. Perform backup operations when the cluster isn't experiencing heavy traffic. Backing up during times of heavy traffic can temporarily degrade application performance and increase the length of time of the backup.
- Avoid running a backup during or before a scheduled maintenance.

{{< warning title="Backups and high DDL activity" >}}
In some circumstances, a backup can fail during high DDL activity. Avoid performing major DDL operations during scheduled backups or while a backup is in progress. To view active tasks, navigate to **Tasks**.
{{< /warning >}}

{{<index/block>}}

  {{<index/item
    title="Configure backup storage"
    body="Configure the storage location for your backups."
    href="configure-backup-storage/"
    icon="fa-light fa-bucket">}}

  {{<index/item
    title="Schedule universe data backups"
    body="Create backup schedules to regularly back up universe data."
    href="schedule-data-backups/"
    icon="fa-light fa-calendar">}}

  {{<index/item
    title="Back up universe data"
    body="Back up universes and create incremental backups."
    href="back-up-universe-data/"
    icon="fa-light fa-down-to-bracket">}}

  {{<index/item
    title="Restore universe data"
    body="Restore from full and incremental backups."
    href="restore-universe-data/"
    icon="fa-light fa-up-to-bracket">}}

  {{<index/item
    title="Perform point-in-time recovery"
    body="Recover universe data from a specific point in time."
    href="pitr/"
    icon="fa-light fa-timeline-arrow">}}

  {{<index/item
    title="Disaster recovery"
    body="Fail over to a backup universe in case of unplanned outages."
    href="disaster-recovery/"
    icon="fa-light fa-sun-cloud">}}

{{</index/block>}}
