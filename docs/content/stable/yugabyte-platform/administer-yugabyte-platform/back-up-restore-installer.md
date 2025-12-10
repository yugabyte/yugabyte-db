---
title: Back up and restore YugabyteDB Anywhere using yba-ctl
headerTitle: Back up and restore YugabyteDB Anywhere
description: Back up and restore YugabyteDB Anywhere using YBA Installer.
headcontent: Back up your YugabyteDB Anywhere installation
linkTitle: Back up YugabyteDB Anywhere
menu:
  stable_yugabyte-platform:
    identifier: back-up-restore-2-installer
    parent: administer-yugabyte-platform
    weight: 30
type: docs
---

Your YugabyteDB Anywhere installation includes provider configurations, KMS configurations, certificates, users, roles, and other components required for managing YugabyteDB universes.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../back-up-restore-yba/" class="nav-link">
      <i class="fa-solid fa-cloud"></i>
      YugabyteDB Anywhere
    </a>
  </li>

  <li>
    <a href="../back-up-restore-installer/" class="nav-link active">
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

If you installed YugabyteDB Anywhere using [YBA installer](../../install-yugabyte-platform/install-software/installer/), use the [yba-ctl](../../install-yugabyte-platform/install-software/installer/#download-yba-installer) CLI to back up and restore your YugabyteDB Anywhere installation. The CLI executes the `yb_platform_backup.sh` script to back up an existing YugabyteDB Anywhere server and restore it, when needed, for disaster recovery or migrating to a new server.

## Back up YugabyteDB Anywhere

To back up your YugabyteDB Anywhere installation, perform the following steps:

1. Run the `createbackup` command, as follows:

    ```sh
    sudo yba-ctl createBackup <output_path> [flags]
    ```

    The `createBackup` command executes the `yb_platform_backup.sh` script to create a backup of your YugabyteDB Anywhere instance. Specify the `output_path` where you want the backup `.tgz` file to be stored.

    The `createBackup` command creates a timestamped `tgz` file for the backup. For example:

    ```sh
    sudo yba-ctl createBackup ~/test_backup
    ls test_backup/
    ```

    ```output
    backup_23-04-25-16-54.tgz
    ```

1. Verify that the backup file, with the correct timestamp, is in the specified output directory.

1. Upload the backup file to your preferred storage location and delete it from the local disk.

### Backup options

The following table describes optional flags you can include with the `createBackup` command.

| <div style="width:150px">Flag</div> | Description | Default |
| :--- | :---------- | :------ |
| --disable_version_check | Exclude version metadata when creating backup. | false |
| --exclude_prometheus | Exclude Prometheus metric data from backup. | false |
| --exclude_releases | Exclude YugabyteDB releases from backup. | false |
| -h, --help | Help for `createBackup`. | |
| --skip_restart | Don't restart processes during execution. | true |
| --verbose | Display extra information in the output. | false |
| -f, --force (global flag) | Run in non-interactive mode. All user confirmations are skipped. | |
| --log_level (global flag) | Log level for this command.<br>Levels: panic, fatal, error, warn, info, debug, trace. | "info" |

## Restore YugabyteDB Anywhere

To restore the YugabyteDB Anywhere content from your saved backup, perform the following:

1. If YugabyteDB Anywhere is not installed, [install it](../../install-yugabyte-platform/install-software/installer/).

1. Copy the backup file from your storage location.

1. Run the `restoreBackup` command as follows:

    ```sh
    sudo yba-ctl restoreBackup <input_path> [flags]
    ```

    Specify the `input_path` to the backup file as the only argument. For example:

    ```sh
    sudo yba-ctl restoreBackup ~/test_backup/backup_23-04-25-16-64.tgz
    ```

The `restoreBackup` command executes the `yb_platform_backup.sh` script to restore from the specified backup of your YugabyteDB Anywhere instance.

When finished, the restored YugabyteDB Anywhere is ready to continue managing your universes and clusters.

### Restore options

The following table describes optional flags you can include with the `restoreBackup` command.

| Flag | Description | Default |
| :--- | :---------- | :------ |
| -h, --help | Help for `restoreBackup`. | |
| --migration | Restore from a Replicated installation. For information on migrating from Replicated, refer to [Migrate from Replicated](/v2.20/yugabyte-platform/install-yugabyte-platform/migrate-replicated/). | false |
| --skip_dbdrop | Skip dropping the YugabyteDB Anywhere database before a migration restore. Valid only if --migration is true. | false |
| --skip_restart | Don't restart processes during command execution. | true |
| &#8209;&#8209;use_system_pg | Use system path's `pg_restore` as opposed to installed binary. | false |
| --verbose | Display extra information in the output. | false |
| -f, --force (global flag) | Run in non-interactive mode. All user confirmations are skipped. | |
| --log_level (global flag) | Log level for this command.<br>Levels: panic, fatal, error, warn, info, debug, trace. | "info" |
