---
title: Back up and restore YugabyteDB Anywhere
headerTitle: Back up and restore YugabyteDB Anywhere
linkTitle: Back up YugabyteDB Anywhere
description: Use a script to back up and restore YugabyteDB Anywhere.
menu:
  stable_yugabyte-platform:
    identifier: back-up-restore-yp
    parent: administer-yugabyte-platform
    weight: 10
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../back-up-restore-yp/" class="nav-link active">
      <i class="fa-solid fa-cloud"></i>
      Default
    </a>
  </li>

  <li>
    <a href="../back-up-restore-k8s/" class="nav-link">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

</ul>

YugabyteDB Anywhere installations include configuration settings, certificates and keys, as well as other components required for creating and managing YugabyteDB universes.

You can use the YugabyteDB Anywhere backup script to back up an existing YugabyteDB Anywhere server and restore it, when needed, for disaster recovery or migrating to a new server.

## Back up a YugabyteDB Anywhere server

To back up the YugabyteDB Anywhere server, perform the following:

- Copy the YugabyteDB Anywhere backup script `yb_platform_backup.sh` from the `yugabyte-db` repository using the following `wget` command:

    ```sh
    wget https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/managed/devops/bin/yb_platform_backup.sh
    ```

- Run the `yb_platform_backup.sh` script using the `backup` command, as follows:

    ```sh
    ./yb_platform_backup.sh create --output <output_path> [--data_dir <data_dir>] [--exclude_prometheus]
    ```

    <br>*create* runs the backup of the YugabyteDB Anywhere server.<br>

    *output_path* specifies the location for the `.tar` output file.<br>

    *data_dir* is optional. It specifies the data directory to be backed up. Default is `/opt/yugabyte`.<br>

    *--exclude_prometheus* is optional. It excludes Prometheus metrics from the backup.

- Verify that the backup `.tar` file, with the correct timestamp, is in the specified output directory.

- Upload the backup file to your preferred storage location and delete it from the local disk.

## Restore a YugabyteDB Anywhere server

To restore the YugabyteDB Anywhere content from your saved backup, perform the following:

- Copy the `yb_platform_backup.sh` script from the `yugabyte-db` repository using the following `wget` command:

    ```sh
    wget https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/managed/devops/bin/yb_platform_backup.sh
    ```

- Copy the backup `.tar` file from your storage location.

- Run the `yb_platform_backup.sh` script using the `restore` command:

    ```sh
    ./yb_platform_backup.sh restore --input <input_path> [--destination <destination>]
    ```

    <br>*restore* restores the YugabyteDB Anywhere content.<br>

    *input_path* is the path to the input `.tar` file.<br>

    *destination* is optional. It specifies the output location for data. Default is `/opt/yugabyte`.

Upon completion of the preceding steps, the restored YugabyteDB Anywhere is ready to continue managing your universes and clusters.
