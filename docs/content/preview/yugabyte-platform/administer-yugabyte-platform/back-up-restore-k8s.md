---
title: Back up and restore YugabyteDB Anywhere
headerTitle: Back up and restore YugabyteDB Anywhere
description: Use a script to back up and restore YugabyteDB Anywhere on Kubernetes.
linkTitle: Back up YugabyteDB Anywhere
headcontent: Back up your YugabyteDB Anywhere installation
menu:
  preview_yugabyte-platform:
    identifier: back-up-restore-k8s
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
    <a href="../back-up-restore-yp/" class="nav-link">
      <i class="fa-solid fa-cloud"></i>
      Replicated
    </a>
  </li>

  <li>
    <a href="../back-up-restore-k8s/" class="nav-link active">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>
</ul>

You can use the YugabyteDB Anywhere `yb_platform_backup.sh` script to back up an existing YugabyteDB Anywhere server and restore it, when needed, for disaster recovery or migrating to a new server.

{{< note title="Note" >}}

You cannot back up and restore Prometheus metrics data.

{{< /note >}}

## Download the script

Download the version of the backup script that corresponds to the version of YugabyteDB Anywhere that you are backing up and restoring.

For example, if you are running version {{< yb-version version="preview">}}, you can copy the `yb_platform_backup.sh` script from the `yugabyte-db` repository using the following `wget` command:

```sh
wget https://raw.githubusercontent.com/yugabyte/yugabyte-db/v{{< yb-version version="preview">}}/managed/devops/bin/yb_platform_backup.sh
```

If you are running a different version of YugabyteDB Anywhere, replace the version number in the command with the correct version number.

## Back up a YugabyteDB Anywhere server

You can back up the YugabyteDB Anywhere server as follows:

- Verify that the computer performing the backup operation can access the YugabyteDB Anywhere Kubernetes pod instance by executing the following command:

  ```sh
  kubectl exec --namespace <k8s_namespace> -it <k8s_pod> -c yugaware -- cat /opt/yugabyte/yugaware/README.md
  ```

  *k8s_namespace* specifies the Kubernetes namespace where the YugabyteDB Anywhere pod is running.

  *k8s_pod* specifies the name of the YugabyteDB Anywhere Kubernetes pod.

- Run the `yb_platform_backup.sh` script using the `create` command, as follows:

  ```sh
  ./yb_platform_backup.sh create --output <output_path> --k8s_namespace <k8s_namespace> --k8s_pod <k8s_pod> [--exclude_releases --verbose]
  ```

  *backup* is the command to run the backup of the YugabyteDB Anywhere server.

  *output_path* specifies the location for the output backup archive.

  *k8s_namespace* specifies the Kubernetes namespace in which the YugabyteDB Anywhere pod is running.

  *k8s_pod* specifies the name of the YugabyteDB Anywhere Kubernetes pod.

  *exclude_releases* excludes YugabyteDB releases from the backup archive.

  *verbose* prints debug output.

- Verify that the backup `.tar.gz` file, with the correct timestamp, is in the specified output directory.

- Upload the backup file to your preferred storage location, and delete it from the local disk.

## Restore a YugabyteDB Anywhere server

To restore the YugabyteDB Anywhere content from your saved backup, perform the following:

- Execute the following command to verify that the computer performing the backup operation can access the YugabyteDB Anywhere Kubernetes pod instance:

    ```sh
    kubectl exec --namespace <k8s_namespace> -it <k8s_pod> -c yugaware -- cat /opt/yugabyte/yugaware/README.md
    ```

    *k8s_namespace* specifies the Kubernetes namespace where the YugabyteDB Anywhere pod is running.

    *k8s_pod* specifies the name of the YugabyteDB Anywhere Kubernetes pod.

- Run the `yb_platform_backup.sh` script using the `restore` command:

    ```sh
    ./yb_platform_backup.sh restore --input <input_path> --k8s_namespace <k8s_namespace> --k8s_pod <k8s_pod> [--verbose]
    ```

    *restore* restores the YugabyteDB Anywhere server content.

    *input_path* is the path to the `.tar.gz` backup file to restore.

    *k8s_namespace* specifies the Kubernetes namespace where the YugabyteDB Anywhere pod is running.

    *k8s_pod* specifies the name of the YugabyteDB Anywhere Kubernetes pod.

    *verbose* prints debug output.

Upon completion of the preceding steps, the restored YugabyteDB Anywhere is ready to continue managing your universes and clusters.
