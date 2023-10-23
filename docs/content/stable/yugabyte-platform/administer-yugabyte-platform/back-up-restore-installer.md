---
title: Back up and restore YugabyteDB Anywhere
headerTitle: Back up and restore YugabyteDB Anywhere
description: Use a script to back up and restore YugabyteDB Anywhere on YBA Installer.
linkTitle: Back up YugabyteDB Anywhere
menu:
  stable_yugabyte-platform:
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

You can use the YBA Installer [yba-ctl](../../install-yugabyte-platform/install-software/installer/#download-yba-installer) script which internally executes the `yb_platform_backup.sh` script to back up an existing YugabyteDB Anywhere server and restore it, when needed, for disaster recovery or migrating to a new server.

{{< note title="Note" >}}

You cannot back up and restore Prometheus metrics data.

{{< /note >}}

## Back up YBA if installed using YBA Installer

If you installed YugabyteDB Anywhere using [YBA installer](../../install-yugabyte-platform/install-software/installer/), perform the following steps:

1. Run the `yba-ctl` script using the `createbackup` command, as follows:

    ```sh
    ./yba-ctl.sh createBackup <output_path> [flags]
    ```

    The `createBackup` command executes the `yb_platform_backup.sh` script to create a backup of your YugabyteDB Anywhere instance. Executing this command requires that you specify the `output_path` where you want the backup `.tar.gz` file to be stored as the first argument to `createBackup`.

1. Verify that the backup `.tar.gz` file, with the correct timestamp, is in the specified output directory.

1. Upload the backup file to your preferred storage location and delete it from the local disk.

## Restore YBA if installed using YBA Installer

To restore the YugabyteDB Anywhere content from your saved backup, perform the following:

1. Copy the backup `.tar` file from your storage location.

1. Run the `yba-ctl` script using the `restoreBackup` command:

    ```sh
    yba-ctl restoreBackup <input_path> [flags]
    ```

    The `restoreBackup` command executes the `yb_platform_backup.sh` to restore from a previously taken backup of your YugabyteDB Anywhere instance. Executing this command requires that you specify the `input_path` to the backup `.tar.gz` file as the only argument to `restoreBackup`.

Upon completion of the preceding steps, the restored YugabyteDB Anywhere is ready to continue managing your universes and clusters.