---
title: Back up and restore Yugabyte Platform
headerTitle: Back up and restore Yugabyte Platform
linkTitle: Back up and restore Yugabyte Platform
description: Use a script file to back up and restore Yugabyte Platform.
menu:
  latest:
    identifier: back-up-restore-yp
    parent: enterprise-edition
    weight: 747
isTocNested: true
showAsideToc: true
---

Yugabyte Platform installations include configuration settings, certificates and keys, and other metadata required for orchestrating and managing YugabyteDB universes and clusters.

Follow the procedures below to use the Yugabyte Platform backup script (`yb_platform_backup.sh`) to back up Yugabyte Platform metadata from an existing installation and restore it to create a new Yugabyte Platform installation on an new server.

## Back up metadata

1. Copy the the `yb_platform_backup.sh` script from the yugabyte-db repository using the following `wget` command:

    ```sh
    $ wget https://github.com/yugabyte/yugabyte-db/blob/master/managed/devops/bin/yb_platform_backup.sh 
    ```

2. Run the `yb_platform_backup.sh` script using the `backup` command:

    ```sh
    ./yb_platform_backup.sh backup --output <output_path> [--data_dir <data_dir>] [--exclude_prometheus]
    ```

    `backup`: Command to run the back up of the metadata.
    *output_path*: location for the `.tar` output file.
    *data_dir*: [optional] Specifies the data directory to be backed up. Default is `/opt/yugabyte`.
    *--exclude_prometheus* : [optional] Flag to exclude Prometheus metrics from the backup.

3. Verify that the backup `.tar` file, with the correct timestamp, is in the specified output directory.
4. Upload the backup file to your preferred storage location and delete it from the local disk.

## Restore metadata to new installation

After you have completed a new [YugaWare (or the YugabyteDB Admin Console) installation](../install-admin-console/) on a new node, follow the steps below to restore (or add) the metadata backed up from an existing installation.

1. Copy the the `yb_platform_backup.sh` script from the yugabyte-db repository using the following `wget` command:

    ```sh
    $ wget https://github.com/yugabyte/yugabyte-db/blob/master/managed/devops/bin/yb_platform_backup.sh 
    ```

2. Copy the backup `.tar` file from your storage location.

3. Run the `yb_platform_backup.sh` script using the `restore` command:

```sh
$ ./yb_platform_backup.sh restore --input <input_path> [--destination <destination>]
```

- `restore`: Cmmand to restore the metadata
- *input_path*: Path to the input `.tar` file.
- *destination* : [optional] Specifies the output location for data on the new node. Default is `/opt/yugabyte`.

Your new installation of the YugabyteDB Admin Console is ready to begin orchestrating and managing universes and clusters on the new node.

