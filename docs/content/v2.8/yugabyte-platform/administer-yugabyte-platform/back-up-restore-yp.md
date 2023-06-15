---
title: Back Up and Restore Yugabyte Platform
headerTitle: Back Up and Restore Yugabyte Platform
linkTitle: Back Up and Restore Yugabyte Platform
description: Use a script file to back up and restore Yugabyte Platform.
menu:
  v2.8_yugabyte-platform:
    identifier: back-up-restore-yp
    parent: administer-yugabyte-platform
    weight: 10
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../back-up-restore-yp" class="nav-link active">
      <i class="fa-solid fa-cloud"></i>
      Default
    </a>
  </li>

  <li>
    <a href="../back-up-restore-k8s" class="nav-link">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

</ul>

Yugabyte Platform installations include configuration settings, certificates and keys, and other components required for orchestrating and managing YugabyteDB universes.

You can use the Yugabyte Platform backup script to back up an existing Yugabyte Platform server and restore it, when needed, for disaster recovery or migrating to a new server.

## Download the script

Download the version of the backup script that corresponds to the version of Yugabyte Platform that you are backing up and restoring.

For example, if you are running version {{< yb-version version="v2.8">}}, you can copy the `yb_platform_backup.sh` script from the `yugabyte-db` repository using the following `wget` command:

```sh
wget https://raw.githubusercontent.com/yugabyte/yugabyte-db/v{{< yb-version version="v2.8">}}/managed/devops/bin/yb_platform_backup.sh
```

If you are running a different version of Yugabyte Platform, replace the version number in the command with the correct version number.

## Back Up a Yugabyte Platform Server

To back up the Yugabyte Platform server, perform the following:

- Run the `yb_platform_backup.sh` script using the `backup` command, as follows:

    ```sh
    ./yb_platform_backup.sh create --output <output_path> [--data_dir <data_dir>] [--exclude_prometheus]
    ```

    *create* runs the backup of the Yugabyte Platform server.

    *output_path* specifies the location for the `.tar` output file.

    *data_dir* is optional. It specifies the data directory to be backed up. Default is `/opt/yugabyte`.

    *--exclude_prometheus* is optional. It excludes Prometheus metrics from the backup.

- Verify that the backup `.tar` file, with the correct timestamp, is in the specified output directory.

- Upload the backup file to your preferred storage location and delete it from the local disk.

## Restore a Yugabyte Platform Server

To restore the Yugabyte Platform content from your saved backup, perform the following:

- Copy the backup `.tar` file from your storage location.

- Run the `yb_platform_backup.sh` script using the `restore` command:

    ```sh
    ./yb_platform_backup.sh restore --input <input_path> [--destination <destination>]
    ```

    *restore* restores the Yugabyte Platform content.

    *input_path* is the path to the input `.tar` file.

    *destination* is optional. It specifies the output location for data. Default is `/opt/yugabyte`.

Upon completion of the preceding steps, the restored Yugabyte Platform is ready to continue orchestrating and managing your universes and clusters.
