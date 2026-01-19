---
title: Back up and restore YugabyteDB Anywhere
headerTitle: Back up and restore YugabyteDB Anywhere
description: Use a script to back up and restore YugabyteDB Anywhere.
headcontent: Back up your YugabyteDB Anywhere installation
linkTitle: Back up YugabyteDB Anywhere
menu:
  stable_yugabyte-platform:
    identifier: back-up-restore-1-yba
    parent: administer-yugabyte-platform
    weight: 30
aliases:
  - /stable/yugabyte-platform/administer-yugabyte-platform/back-up-restore-yp/
type: docs
---

YugabyteDB Anywhere installations include configuration settings, certificates and keys, and other components required for creating and managing YugabyteDB universes.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../back-up-restore-yba/" class="nav-link active">
      <i class="fa-solid fa-cloud"></i>
      YugabyteDB Anywhere
    </a>
  </li>
  <li>
    <a href="../back-up-restore-installer/" class="nav-link">
      <i class="fa-solid fa-building"></i>
      YBA Installer</a>
  </li>
  <li>
    <a href="../back-up-restore-k8s/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

</ul>

{{<tags/feature/tp idea="1429">}}If you aren't running [high availability](../high-availability/), use automated backups to take regularly scheduled backups of your YugabyteDB Anywhere installation for recovery in case of the loss of the node running your YugabyteDB Anywhere instance.

You can also perform ad hoc manual backups.

While in Tech Preview, automated YugabyteDB Anywhere backups are not available by default. To make the feature available, use the following [API request](../../anywhere-automation/anywhere-api/):

```sh
curl --request PUT \
--url http://<YBA_IP>/api/v1/customers/<customer-uuid>/runtime_config/00000000-0000-0000-0000-000000000000/key/yb.ui.feature_flags.continuous_platform_backups \
--header 'Content-Type: text/plain' \
--header 'X-AUTH-YW-API-TOKEN: <api-token>' \
--data true
```

## Set up automatic backups

Before you can set up automatic backups of YugabyteDB Anywhere, you need to [configure a storage location](../../back-up-restore-universes/configure-backup-storage/) for the backups.

Automatic backups do not include universe Prometheus data, but they do include locally stored YugabyteDB releases.

To configure automatic backups of your YugabyteDB Anywhere installation, do the following:

1. Navigate to **Admin>Platform HA and Backups** and select **Automated Platform Backups**.
1. Click **Set up automated platform backups** (or **Edit** if already configured).
1. Select the storage configuration you want to use for the backup. For more information, see [Configure backup storage](../../back-up-restore-universes/configure-backup-storage/).
1. Enter a name for the folder where you want to store the backups.

    If you are backing up more than one YugabyteDB Anywhere instance to the same storage, provide a name that uniquely identifies the instance you are backing up. For example, `yba_104.72.167.2_backups`.

1. Enter the backup frequency in minutes. 5 minutes works well for most purposes. More frequent backups provide more recovery points but may impact performance.
1. Click **Apply Changes**.

Click **Remove** to disable periodic backups (this stops automatic backups but does not delete existing backup files).

## Back up manually

You can manually back up your YugabyteDB Anywhere installation. Manual backups can include Prometheus data and YugabyteDB releases, in addition to your YugabyteDB Anywhere settings and metadata.

Manual backups are stored locally on the node that hosts your installation.

Perform a manual backup:

- Before upgrading YugabyteDB Anywhere.
- When migrating YugabyteDB Anywhere to a different host.
- When you need to include Prometheus metrics data.
- For ad-hoc backups outside your periodic backup schedule.

To create a one-time backup:

1. Navigate to **Admin>Platform HA and Backups** and select **Automated Platform Backups**.
1. Click **One-Time Platform Export**.
1. Select the data you want to back up.

    - **Platform Metadata**: YugabyteDB Anywhere instance settings and metadata.
    - **Local YugabyteDB Releases**: [YugabyteDB releases](../../manage-deployments/ybdb-releases/) stored on universe nodes.
    - **Universe Metrics (Prometheus Data)**: Prometheus metrics data for your universes.

1. Specify the destination for the backup, as a full path. For example, `/opt/yugabyte/yba_backups` or `/tmp/yba_export`.

    Ensure the directory exists and the `yugabyte` user has write permissions for the location.

    The backup is saved as a `.tar.gz` file with a timestamped name.

1. Click **Export**.

    You can monitor progress of the backup on the **Tasks** page.

## Restore YugabyteDB Anywhere

When restoring YugabyteDB Anywhere (for example, after the loss of the node running YugabyteDB Anywhere):

1. Create an fresh YugabyteDB Anywhere installation, either on the existing node after it is recovered, or on a new node.
1. Using the new YugabyteDB Anywhere instance, restore from the most recent backup.

When doing a restore, YugabyteDB Anywhere performs the following checks:

- Existing universes. By default, due to the possibility of data loss, you can only do a restore if your instance is not managing any universes. You can override this by setting the **Allow YBA Restore With Universes** Global Runtime Configuration option (config key `yb.yba_backup.allow_restore_with_universes`) to true.

- The selected backup is not older than one day. By default you cannot restore from backups older than one day, as the backup may be inconsistent if you performed management operations after the backup was taken. You can override this by setting the **Allow YBA Restore With Old Backup** Global Runtime Configuration option (config key `yb.yba_backup.allow_restore_with_old_backup`) to true.

Refer to [Manage runtime configuration settings](../../administer-yugabyte-platform/manage-runtime-config/). Note that only a Super Admin user can modify Global configuration settings.

### Restore from automated backups

To restore a YugabyteDB Anywhere backup from external storage:

1. If YugabyteDB Anywhere is not installed, [install it](../../install-yugabyte-platform/install-software/installer/).
1. Navigate to **Admin>Platform HA and Backups** and select **Automated Platform Backups**.
1. Click **Advanced Restore**.

    If YugabyteDB Anywhere detects existing universes, a warning is shown about potential data loss.

1. Select **Cloud Backup**.
1. Select the storage configuration for the backup you want to restore.
1. Enter the name of the folder that you specified for the periodic backup you are restoring (for example, `yba_104.72.167.2_backups`).
1. Click **Restore**.

YugabyteDB Anywhere restores the most recent backup and restarts automatically after the restore finishes.

### Restore from a manual backup

To restore a YugabyteDB Anywhere backup from a manual backup:

1. If YugabyteDB Anywhere is not installed, [install it](../../install-yugabyte-platform/install-software/installer/).
1. Navigate to **Admin>Platform HA and Backups** and select **Automated Platform Backups**.
1. Click **Advanced Restore**.

    If YugabyteDB Anywhere detects existing universes, a warning is shown about potential data loss.

1. Select **Local Backup**.
1. Enter the full file path to the backup file.

    For example, `/opt/yba_backups/yba_backup_20240115_120000.tar.gz`.

1. Click **Restore**.

YugabyteDB Anywhere restores from the backup and restarts automatically after the restore finishes.
