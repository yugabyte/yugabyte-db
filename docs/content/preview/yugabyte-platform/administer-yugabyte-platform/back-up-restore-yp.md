---
title: Back up and restore YugabyteDB Anywhere
headerTitle: Back up and restore YugabyteDB Anywhere
description: Use a script to back up and restore YugabyteDB Anywhere.
headcontent: Back up your YugabyteDB Anywhere installation
linkTitle: Back up YugabyteDB Anywhere
menu:
  preview_yugabyte-platform:
    identifier: back-up-restore-yp
    parent: administer-yugabyte-platform
    weight: 30
type: docs
---

YugabyteDB Anywhere installations include configuration settings, certificates and keys, and other components required for creating and managing YugabyteDB universes.

<ul class="nav nav-tabs-alt nav-tabs-yb">
 <li>
    <a href="../back-up-restore-installer/" class="nav-link">
      <i class="fa-solid fa-building"></i>
      YBA Installer</a>
  </li>
  <li >
    <a href="../back-up-restore-yp/" class="nav-link active">
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

You can use the YugabyteDB Anywhere `yb_platform_backup.sh` script to back up an existing YugabyteDB Anywhere server and restore it, when needed, for disaster recovery or migrating to a new server.

## Prerequisites

To perform backups and restores in a Replicated environment, you must have a permission to run `docker` commands. This means that on systems with the default `docker` configuration, the `yb_platform_backup.sh` backup and restore script must be run using `sudo` or run as the `root` user (or another member of the `docker` group).

## Download the script

Download the version of the backup script that corresponds to the version of YugabyteDB Anywhere that you are backing up and restoring.

For example, if you are running version {{< yb-version version="preview">}}, you can copy the `yb_platform_backup.sh` script from the `yugabyte-db` repository using the following `wget` command:

```sh
wget https://raw.githubusercontent.com/yugabyte/yugabyte-db/v{{< yb-version version="preview">}}/managed/devops/bin/yb_platform_backup.sh
```

If you are running a different version of YugabyteDB Anywhere, replace the version number in the command with the correct version number.

## Back up a YugabyteDB Anywhere server

To back up a YugabyteDB Anywhere server, perform the following:

1. Run the `yb_platform_backup.sh` script using the `backup` command, as follows:

    ```sh
    ./yb_platform_backup.sh create --output <output_path> [--data_dir <data_dir>] [--exclude_prometheus]
    ```

    The `create` command runs the backup of the YugabyteDB Anywhere server.

    `--output_path` specifies the location (absolute path) for the `.tar` output file.

    `--data_dir` specifies the data directory to be backed up. Default is `/opt/yugabyte`. Use this flag if YugabyteDB Anywhere is not installed in the default location.

    `--exclude_prometheus` excludes Prometheus metrics from the backup. Optional.

    {{<note title="Note">}}

If you are using versions 2.18.9, 2.20.6, or 2024.1.2 or earlier, add the `--disable_version_check` flag if you are specifying a custom data directory (that is, you are not using `/opt/yugabyte`).

    {{</note>}}

1. Verify that the backup `.tar` file, with the correct timestamp, is in the specified output directory.

1. Upload the backup file to your preferred storage location and delete it from the local disk.

## Restore a YugabyteDB Anywhere server

To restore the YugabyteDB Anywhere content from your saved backup, perform the following:

1. Copy the backup `.tar` file from your storage location.

1. Run the `yb_platform_backup.sh` script using the `restore` command:

    ```sh
    ./yb_platform_backup.sh restore --input <input_path> [--destination <destination>]
    ```

    The `restore` command restores the YugabyteDB Anywhere content.

    `input_path` is the path to the input `.tar` file.

    `destination` is optional. It specifies the output location for data. Default is `/opt/yugabyte`.

Upon completion of the preceding steps, the restored YugabyteDB Anywhere is ready to continue managing your universes and clusters.
