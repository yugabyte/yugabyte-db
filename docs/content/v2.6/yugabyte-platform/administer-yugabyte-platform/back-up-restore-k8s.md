---
title: Back Up and Restore Yugabyte Platform on Kubernetes
headerTitle: Back Up and Restore Yugabyte Platform on Kubernetes
description: Use a script file to back up and restore Yugabyte Platform on Kubernetes.
linkTitle: Back Up and Restore Yugabyte Platform
menu:
  v2.6:
    identifier: back-up-restore-k8s
    parent: administer-yugabyte-platform
    weight: 20
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="/latest/yugabyte-platform/administer-yugabyte-platform/back-up-restore-yp" class="nav-link">
      <i class="fas fa-cloud"></i>
      Default
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/administer-yugabyte-platform/back-up-restore-k8s" class="nav-link active">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

</ul>

Yugabyte Platform installations include configuration settings, certificates and keys, and other components required for orchestrating and managing YugabyteDB universes.

You can use the Yugabyte Platform backup script to back up an existing Yugabyte Platform server and restore it, when needed, for disaster recovery or migrating to a new server.

{{< note title="Note" >}}

You cannot back up and restore Prometheus metrics data.

{{< /note >}}

The Yugabyte Platform console is used in a highly available mode and orchestrates and manages YugabyteDB universes, or clusters, on one or more regions (across public cloud and private on-premises data centers). For details, see [Yugabyte Platform overview](/latest/yugabyte-platform/overview/).

## Back Up a Yugabyte Platform Server

You can back up the Yugabyte Platform server as follows:

- Copy the the Yugabyte Platform backup script `yb_platform_backup.sh` from the yugabyte-db repository to your local workstation using the following wget command:

  ```sh
  wget https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/managed/devops/bin/yb_platform_backup.sh
  ```

- Verify that the computer performing the backup operation can access the Yugabyte Platform kubernetes pod instance by executing the following command:

  ```sh
  kubectl exec --namespace <k8s_namespace> -it <k8s_pod> -c yugaware -- cat /opt/yugabyte/yugaware/README.md
  ```
  
  *k8s_namespace* specifies the kubernetes namespace where the Yugabyte Platform pod is running.<br>
  *k8s_pod* specifies the name of the Yugabyte Platform kubernetes pod.

- Run the `yb_platform_backup.sh` script using the `create` command, as follows:

  ```sh
  ./yb_platform_backup.sh create --output <output_path> --k8s_namespace <k8s_namespace> --k8s_pod <k8s_pod> [--exclude_releases --verbose]
  ```
  *backup* is the command to run the backup of the Yugabyte Platform server.<br>
  
  *output_path* specifies the location for the output backup archive.<br>
  
  *k8s_namespace* specifies the Kubernetes namespace in which the Yugabyte Platform pod is running.<br>
  
  *k8s_pod* specifies the name of the Yugabyte Platform Kubernetes pod.<br>
  
  *exclude_releases* excludes Yugabyte releases from the backup archive.<br>
  
  *verbose* prints debug output.<br>
  
- Verify that the backup `.tar.gz` file, with the correct timestamp, is in the specified output directory.

- Upload the backup file to your preferred storage location, and delete it from the local disk.

## Restore a Yugabyte Platform Server

To restore the Yugabyte Platform content from your saved backup, perform the following:

- Copy the the Yugabyte Platform backup script yb_platform_backup.sh` from the yugabyte-db repository to your local workstation using the following wget command:

    ```sh
    wget https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/managed/devops/bin/yb_platform_backup.sh
    ```

- Execute the following command to verify that the computer performing the backup operation can access the Yugabyte Platform kubernetes pod instance:

    ```sh
    kubectl exec --namespace <k8s_namespace> -it <k8s_pod> -c yugaware -- cat /opt/yugabyte/yugaware/README.md
    ```

    *k8s_namespace* specifies the kubernetes namespace where the Yugabyte Platform pod is running.<br>

    *k8s_pod* specifies the name of the Yugabyte Platform kubernetes pod.

- Run the `yb_platform_backup.sh` script using the `restore` command:

    ```sh
    ./yb_platform_backup.sh restore --input <input_path> --k8s_namespace <k8s_namespace> --k8s_pod <k8s_pod> [--verbose]
    ```

    *restore* restores the Yugabyte Platform server content.<br>

    *input_path* is the path to the `.tar.gz` backup file to restore.<br>

    *k8s_namespace* specifies the kubernetes namespace where the Yugabyte Platform pod is running.<br>

    *k8s_pod* specifies the name of the Yugabyte Platform Kubernetes pod.<br>

    *verbose* prints debug output.<br>

Upon completion of the preceding steps, the restored Yugabyte Platform is ready to continue orchestrating and managing your universes and clusters.