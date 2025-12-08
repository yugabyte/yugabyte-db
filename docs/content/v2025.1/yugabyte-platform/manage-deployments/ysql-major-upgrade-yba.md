---
title: YSQL major upgrade in YugabyteDB Anywhere
headerTitle: YSQL major upgrade
linkTitle: YSQL major upgrade
description: Upgrade YugabyteDB to PostgreSQL 15 version in YBA
headcontent: Upgrade the YugabyteDB software on your universe to a version that supports PG15
menu:
  v2025.1_yugabyte-platform:
    identifier: ysql-major-upgrade-yba
    parent: upgrade-software
    weight: 50
type: docs
---

Upgrading YugabyteDB from a version based on PostgreSQL 11 ({{<release "2024.2">}} and earlier) to a version based on PostgreSQL 15 ({{<release "2025.1">}} or later) requires additional steps.

The upgrade is fully online. While the upgrade is in progress, you have full and uninterrupted read and write access to your cluster.

## Before you begin

- All DDL statements, except ones related to [Temporary table](../../../api/ysql/the-sql-language/creating-and-using-temporary-schema-objects/temporary-tables-views-sequences-and-indexes/) and [Refresh Materialized View](../../../api/ysql/the-sql-language/statements/ddl_refresh_matview/) are blocked for the duration of the upgrade. Consider executing all DDLs before the upgrade, and pause any jobs that might run DDLs. DMLs are allowed.
- Upgrade client drivers.

    Upgrade all application client drivers to the new version. The client drivers are backwards compatible, and work with both the old and new versions of the database.
- Your cluster must be running {{<release "2024.2.3.0">}} or later.

    If you have a pre-existing cluster, first upgrade it to the latest version in the v2024.2 series using the [upgrade instructions](../upgrade-software/).

- To update Kubernetes universes to v2025.1 or later, you must be running YugabyteDB Anywhere v2025.1.0.1 or later.

### Precheck

New PostgreSQL major versions add many new features and performance improvements, but also remove some older unsupported features and data types. You can only upgrade after you remove all deprecated features and data types from your databases.

Use the Pre-Check to make sure your cluster is compatible with the new version.

To perform the pre-check, do the following:

1. Navigate to **Universes** and select your universe.

1. Click **Actions > Upgrade Database Version** to display the **Upgrade Database** dialog.

1. Click **Run Pre-Check Only**.

Results are displayed in the task details. To view tasks, navigate to your universe **Tasks** tab.

After a successful upgrade precheck, you can proceed with the usual database upgrade.

{{<tip title="Backup">}}
Back up your cluster at this time. Refer to [Backup](../../../reference/configuration/yugabyted/#backup).
{{</tip>}}

## Limitations

During the upgrade process, until the upgrade is finalized or rolled back, the following operations are not allowed:

- DDLs
- G-Flags changes
- Restore a backup or point-in-time-recovery (PITR)
- Configure YSQL
- Update YSQL usernames and passwords
- Create or restart xCluster replication
- xCluster DR operations, including creating, restarting, switchover, failover, or changes to tables or database in replication
- Changes to audit logging

Keep in mind the following additional caveats for backups and PITR:

- Backups taken during the monitoring phase of a YSQL major upgrade cannot be restored on the same universe after rollback. Backups taken before the upgrade can be used for restore.

- You can't perform PITR on the universe to a time when the upgrade was running. This applies even if the upgrade was rolled back. After an upgrade is finalized, you cannot perform PITR to any time before the upgrade.
