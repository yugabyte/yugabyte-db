---
title: Back up and restore YugabyteDB Anywhere on Kubernetes
headerTitle: Back up and restore YugabyteDB Anywhere on Kubernetes
description: Use a script to back up and restore YugabyteDB Anywhere on Kubernetes.
linkTitle: Back up YugabyteDB Anywhere
menu:
  preview_yugabyte-platform:
    identifier: back-up-restore-k8s
    parent: administer-yugabyte-platform
    weight: 20
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../back-up-restore-yp/" class="nav-link">
      <i class="fas fa-cloud"></i>
      Default
    </a>
  </li>

  <li>
    <a href="../back-up-restore-k8s/" class="nav-link active">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>
</ul>

YugabyteDB Anywhere installations include configuration settings, certificates and keys, and other components required for creating and managing YugabyteDB universes.

You can use the YugabyteDB Anywhere backup script to back up an existing YugabyteDB Anywhere server and restore it, when needed, for disaster recovery or migrating to a new server.

{{< note title="Note" >}}

You cannot back up and restore Prometheus metrics data.

{{< /note >}}

The YugabyteDB Anywhere UI is used for creating and managing YugabyteDB universes, or clusters, on one or more regions (across public cloud and private on-premises data centers). For details, see [YugabyteDB Anywhere overview](../../../yugabyte-platform/).

## Back up a YugabyteDB Anywhere server

You can back up the YugabyteDB Anywhere server as follows:

- Copy the YugabyteDB Anywhere backup script `yb_platform_backup.sh` from the `yugabyte-db` repository to your local workstation using the following wget command:

  ```sh
  wget https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/managed/devops/bin/yb_platform_backup.sh
  ```

- Verify that the computer performing the backup operation can access the YugabyteDB Anywhere Kubernetes pod instance by executing the following command:

  ```sh
  kubectl exec --namespace <k8s_namespace> -it <k8s_pod> -c yugaware -- cat /opt/yugabyte/yugaware/README.md
  ```

  <br>*k8s_namespace* specifies the Kubernetes namespace where the YugabyteDB Anywhere pod is running.<br>
  *k8s_pod* specifies the name of the YugabyteDB Anywhere kubernetes pod.

- Run the `yb_platform_backup.sh` script using the `create` command, as follows:

  ```sh
  ./yb_platform_backup.sh create --output <output_path> --k8s_namespace <k8s_namespace> --k8s_pod <k8s_pod> [--exclude_releases --verbose]
  ```

  <br>*backup* is the command to run the backup of the YugabyteDB Anywhere server.<br>

  *output_path* specifies the location for the output backup archive.<br>

  *k8s_namespace* specifies the Kubernetes namespace in which the YugabyteDB Anywhere pod is running.<br>

  *k8s_pod* specifies the name of the YugabyteDB Anywhere Kubernetes pod.<br>

  *exclude_releases* excludes YugabyteDB releases from the backup archive.<br>

  *verbose* prints debug output.<br>

- Verify that the backup `.tar.gz` file, with the correct timestamp, is in the specified output directory.

- Upload the backup file to your preferred storage location, and delete it from the local disk.

## Restore a YugabyteDB Anywhere server

To restore the YugabyteDB Anywhere content from your saved backup, perform the following:

- Copy the YugabyteDB Anywhere backup `script yb_platform_backup.sh` from the `yugabyte-db` repository to your local workstation using the following wget command:

    ```sh
    wget https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/managed/devops/bin/yb_platform_backup.sh
    ```

- Execute the following command to verify that the computer performing the backup operation can access the YugabyteDB Anywhere Kubernetes pod instance:

    ```sh
    kubectl exec --namespace <k8s_namespace> -it <k8s_pod> -c yugaware -- cat /opt/yugabyte/yugaware/README.md
    ```

    <br>*k8s_namespace* specifies the Kubernetes namespace where the YugabyteDB Anywhere pod is running.<br>

    *k8s_pod* specifies the name of the YugabyteDB Anywhere Kubernetes pod.

- Run the `yb_platform_backup.sh` script using the `restore` command:

    ```sh
    ./yb_platform_backup.sh restore --input <input_path> --k8s_namespace <k8s_namespace> --k8s_pod <k8s_pod> [--verbose]
    ```

    <br>*restore* restores the YugabyteDB Anywhere server content.<br>

    *input_path* is the path to the `.tar.gz` backup file to restore.<br>

    *k8s_namespace* specifies the Kubernetes namespace where the YugabyteDB Anywhere pod is running.<br>

    *k8s_pod* specifies the name of the YugabyteDB Anywhere Kubernetes pod.<br>

    *verbose* prints debug output.<br>

Upon completion of the preceding steps, the restored YugabyteDB Anywhere is ready to continue managing your universes and clusters.
