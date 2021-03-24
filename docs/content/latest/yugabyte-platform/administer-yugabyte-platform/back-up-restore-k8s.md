---
title: Back up and restore Platform - Kubernetes
headerTitle: Back up and restore Platform - Kubernetes
linkTitle: Back up and restore Yugabyte Platform - K8s
description: Use a script file to back up and restore Yugabyte Platform on Kubernetes.
aliases:
menu:
  latest:
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

The Yugabyte Platform console is used in a highly available mode and orchestrates and manages YugabyteDB universes, or clusters, on one or more regions (across public cloud and private on-premises data centers). For more details, refer to the [Yugabyte Platform overview](https://docs.yugabyte.com/latest/yugabyte-platform/overview/).

## Back up a Yugabyte Platform server

1. Copy the the Yugabyte Platform backup script (`yb_platform_backup.sh`) from the yugabyte-db repository to your local workstation using the following wget command:

    ```sh
    wget https://github.com/yugabyte/yugabyte-db/blob/master/managed/devops/bin/yb_platform_backup.sh
    ```

1. Verify that the machine performing the backup operation can access the Yugabyte Platform kubernetes pod instance:

    ```sh
    kubectl exec --namespace <k8s_namespace> -it <k8s_pod> -c yugaware -- cat /opt/yugabyte/yugaware/README.md
    ```

    <br/>

    * `k8s_namespace` specifies the kubernetes namespace where the Yugabyte Platform pod is running.
    * `k8s_pod` specifies the name of the Yugabyte Platform kubernetes pod.

1. Run the `yb_platform_backup.sh` script using the `backup` command:

    ```sh
    ./yb_platform_backup.sh backup --output <output_path> --k8s_namespace <k8s_namespace> --k8s_pod <k8s_pod> [--exclude_releases --verbose]
    ```

    * `backup` is the command to run the backup of the Yugabyte Platform server.
    * `output_path` specifies the location for the output backup archive.
    * `k8s_namespace` specifies the Kubernetes namespace in which the Yugabyte Platform pod is running.
    * `k8s_pod` specifies the name of the Yugabyte Platform Kubernetes pod.
    * `exclude_releases` excludes Yugabyte releases from the backup archive.
    * `verbose` prints debug output.

    {{< note title="Prometheus metrics data" >}}

Backup and restore of Prometheus metrics data is not currently supported.

    {{< /note >}}

1. Verify that the backup `.tar.gz` file, with the correct timestamp, is in the specified output directory.

1. Upload the backup file to your preferred storage location, and delete it from the local disk.

## Restore a Yugabyte Platform server

To restore the Yugabyte Platform content from your saved backup, do the following:

1. Copy the the Yugabyte Platform backup script (`yb_platform_backup.sh`) from the yugabyte-db repository to your local workstation using the following wget command:

    ```sh
    wget https://github.com/yugabyte/yugabyte-db/blob/master/managed/devops/bin/yb_platform_backup.sh
    ```

1. Verify that the machine performing the backup operation can access the Yugabyte Platform kubernetes pod instance:

    ```sh
    kubectl exec --namespace <k8s_namespace> -it <k8s_pod> -c yugaware -- cat /opt/yugabyte/yugaware/README.md
    ```

    <br/>

    * `k8s_namespace` specifies the kubernetes namespace where the Yugabyte Platform pod is running.
    * `k8s_pod` specifies the name of the Yugabyte Platform kubernetes pod.

1. Run the `yb_platform_backup.sh` script using the `restore` command:

    ```sh
    ./yb_platform_backup.sh restore --input <input_path> --k8s_namespace <k8s_namespace> --k8s_pod <k8s_pod> [--verbose]
    ```

    <br/>

    * `restore` is the command to restore the Yugabyte Platform server content.
    * `input_path` is the path to the `.tar.gz` backup file to restore.
    * `k8s_namespace` specifies the kubernetes namespace where the Yugabyte Platform pod is running.
    * `k8s_pod` specifies the name of the Yugabyte Platform Kubernetes pod.
    * `verbose` prints debug output.

    {{< note title="Prometheus metrics data" >}}

Backup and restore of Prometheus metrics data is not currently supported.

    {{< /note >}}

Your restored Yugabyte Platform is now ready to continue orchestrating and managing your universes and clusters.
