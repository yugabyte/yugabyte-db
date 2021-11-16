---
title: Back Up and Restore Yugabyte Platform
headerTitle: Back Up and Restore Yugabyte Platform
linkTitle: Back Up and Restore Yugabyte Platform
description: Use a script file to back up and restore Yugabyte Platform.
menu:
  v2.6:
    identifier: back-up-restore-yp
    parent: administer-yugabyte-platform
    weight: 10
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="/latest/yugabyte-platform/administer-yugabyte-platform/back-up-restore-yp" class="nav-link active">
      <i class="fas fa-cloud"></i>
      Default
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/administer-yugabyte-platform/back-up-restore-k8s" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

</ul>

Yugabyte Platform installations include configuration settings, certificates and keys, and other components required for orchestrating and managing YugabyteDB universes.

You can use the Yugabyte Platform backup script to back up an existing Yugabyte Platform server and restore it, when needed, for disaster recovery or migrating to a new server.

## Back Up a Yugabyte Platform Server

To back up the Yugabyte Platform server, perform the following:

- Copy the the Yugabyte Platform backup script `yb_platform_backup.sh` from the yugabyte-db repository using the following `wget` command:

    ```sh
    wget https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/managed/devops/bin/yb_platform_backup.sh 
    ```

- Run the `yb_platform_backup.sh` script using the `backup` command, as follows:

    ```sh
    ./yb_platform_backup.sh create --output <output_path> [--data_dir <data_dir>] [--exclude_prometheus]
    ```

    *create* runs the backup of the Yugabyte Platform server.<br>

    *output_path* specifies the location for the `.tar` output file.<br>

    *data_dir* is optional. It specifies the data directory to be backed up. Default is `/opt/yugabyte`.<br>

    *--exclude_prometheus* is optional. It excludes Prometheus metrics from the backup.

- Verify that the backup `.tar` file, with the correct timestamp, is in the specified output directory.

- Upload the backup file to your preferred storage location and delete it from the local disk.

## Restore a Yugabyte Platform Server

To restore the Yugabyte Platform content from your saved backup, perform the following:

- Copy the the `yb_platform_backup.sh` script from the yugabyte-db repository using the following `wget` command:

    ```sh
    wget https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/managed/devops/bin/yb_platform_backup.sh 
    ```

- Copy the backup `.tar` file from your storage location.

- Run the `yb_platform_backup.sh` script using the `restore` command:

    ```sh
    ./yb_platform_backup.sh restore --input <input_path> [--destination <destination>]
    ```

    *restore* restores the Yugabyte Platform content.<br>

    *input_path* is the path to the input `.tar` file.<br>

    *destination* is optional. It specifies the output location for data. Default is `/opt/yugabyte`.

Upon completion of the preceding steps, the restored Yugabyte Platform is ready to continue orchestrating and managing your universes and clusters.

