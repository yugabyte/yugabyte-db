---
title: Back up and restore YugabyteDB Anywhere
headerTitle: Back up and restore YugabyteDB Anywhere
linkTitle: Back up YugabyteDB Anywhere
description: Use a script to back up and restore YugabyteDB Anywhere.
menu:
  v2.18_yugabyte-platform:
    identifier: back-up-restore-yp
    parent: administer-yugabyte-platform
    weight: 30
type: docs
---

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

YugabyteDB Anywhere installations include configuration settings, certificates and keys, as well as other components required for creating and managing YugabyteDB universes.

You can use the YugabyteDB Anywhere `yb_platform_backup.sh` script to back up an existing YugabyteDB Anywhere server and restore it, when needed, for disaster recovery or migrating to a new server.

## Prerequisites

To perform backups and restores in a Replicated environment, you must have a permission to run `docker` commands. This means that on systems with the default `docker` configuration, the `yb_platform_backup.sh` backup and restore script must be run using `sudo` or run as the `root` user (or another member of the `docker` group).

## Download the script

Download the version of the backup script that corresponds to the version of YugabyteDB Anywhere that you are backing up and restoring.

For example, if you are running version {{< yb-version version="stable">}}, you can copy the `yb_platform_backup.sh` script from the `yugabyte-db` repository using the following `wget` command:

```sh
wget https://raw.githubusercontent.com/yugabyte/yugabyte-db/v{{< yb-version version="stable">}}/managed/devops/bin/yb_platform_backup.sh
```

If you are running a different version of YugabyteDB Anywhere, replace the version number in the command with the correct version number.

## Back up a YugabyteDB Anywhere server

To back up a YugabyteDB Anywhere server, perform the following:

1. Run the `yb_platform_backup.sh` script using the `backup` command, as follows:

    ```sh
    ./yb_platform_backup.sh create --output <output_path> [--data_dir <data_dir>] [--exclude_prometheus]
    ```

    The `create` command runs the backup of the YugabyteDB Anywhere server.

    `output_path` specifies the location for the `.tar` output file.

    `data_dir` is optional. It specifies the data directory to be backed up. Default is `/opt/yugabyte`.

    `--exclude_prometheus` is optional. It excludes Prometheus metrics from the backup.

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
