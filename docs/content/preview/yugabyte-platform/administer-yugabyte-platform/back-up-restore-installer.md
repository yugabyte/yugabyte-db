---
title: Back up and restore YugabyteDB Anywhere
headerTitle: Back up and restore YugabyteDB Anywhere
description: Use a script to back up and restore YugabyteDB Anywhere on YBA Installer.
linkTitle: Back up YugabyteDB Anywhere
menu:
  preview_yugabyte-platform:
    identifier: back-up-restore-installer
    parent: administer-yugabyte-platform
    weight: 10
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../back-up-restore-installer/" class="nav-link active">
      <i class="fa-solid fa-building"></i>
      YBA Installer</a>
  </li>

  <li >
    <a href="../back-up-restore-yp/" class="nav-link">
      <i class="fa-solid fa-cloud"></i>
      Replicated
    </a>
  </li>

  <li>
    <a href="../back-up-restore-k8s/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>
</ul>

YugabyteDB Anywhere installations include configuration settings, certificates and keys, and other components required for creating and managing YugabyteDB universes.

If you installed YBA using [YBA installer](../../install-yugabyte-platform/install-software/installer/), use the [yba-ctl](../../install-yugabyte-platform/install-software/installer/#download-yba-installer) CLI to back up and restore your YBA installation. The CLI executes the `yb_platform_backup.sh` script to back up an existing YugabyteDB Anywhere server and restore it, when needed, for disaster recovery or migrating to a new server.

{{< note title="Note" >}}

You cannot back up and restore Prometheus metrics data.

{{< /note >}}

## Back up YBA

To back up your YugabyteDB Anywhere installation, perform the following steps:

1. Run the `createbackup` command, as follows:

    ```sh
    yba-ctl createBackup <output_path> [flags]
    ```

    The `createBackup` command executes the `yb_platform_backup.sh` script to create a backup of your YugabyteDB Anywhere instance. Specify the `output_path` where you want the backup `.tar.gz` file to be stored.

1. Verify that the backup `.tar.gz` file, with the correct timestamp, is in the specified output directory.

1. Upload the backup file to your preferred storage location and delete it from the local disk.

## Restore YBA

To restore the YugabyteDB Anywhere content from your saved backup, perform the following:

1. Copy the backup `.tar` file from your storage location.

1. Run the `restoreBackup` command as follows:

    ```sh
    yba-ctl restoreBackup <input_path> [flags]
    ```

    The `restoreBackup` command executes the `yb_platform_backup.sh` script to restore from a previously taken backup of your YugabyteDB Anywhere instance. Specify the `input_path` to the backup `.tar.gz` file as the only argument.

When finished, the restored YugabyteDB Anywhere is ready to continue managing your universes and clusters.
