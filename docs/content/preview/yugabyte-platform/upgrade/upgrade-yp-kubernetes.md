---
title: Upgrade YugabyteDB Anywhere installation on Kubernetes
headerTitle: Upgrade YugabyteDB Anywhere
linkTitle: Upgrade installation
description: Upgrade YugabyteDB Anywhere installation on Kubernetes
aliases:
  - /preview/yugabyte-platform/manage-deployments/migrate-to-helm3/
menu:
  preview_yugabyte-platform:
    identifier: upgrade-yp-3-kubernetes
    parent: upgrade
    weight: 80
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../upgrade-yp-installer/" class="nav-link">
      <i class="fa-solid fa-building"></i>YBA Installer</a>
  </li>

  <li>
    <a href="../upgrade-yp-replicated/" class="nav-link">
      <i class="fa-solid fa-cloud"></i>Replicated</a>
  </li>

  <li>
    <a href="../upgrade-yp-kubernetes/" class="nav-link active">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>Kubernetes</a>
  </li>

</ul>

You can use [Helm](https://helm.sh/) to upgrade your YugabyteDB Anywhere (YBA) installed on [Kubernetes](https://kubernetes.io/) to a newer version.

If you are upgrading a YugabyteDB Anywhere installation with high availability enabled, follow the instructions provided in [Upgrade instances](../../administer-yugabyte-platform/high-availability/#upgrade-instances).

Before running an upgrade, execute the following command to obtain the latest versions of YugabyteDB Anywhere from Helm charts:

```sh
helm repo update
```

## Upgrade using Helm

To upgrade to a specific version while preserving overrides you might have applied to your initial YugabyteDB Anywhere installation or previous upgrades, execute the following command:

```sh
helm upgrade yw-test yugabytedb/yugaware --version 2.15.2 -n yb-platform --reuse-values --set image.tag=2.15.2.0-b87 --wait
```

To obtain the value for `--set image.tag`, execute the following command:

```sh
helm list | awk '{if (NR!=1) print $NF}'
```

If you do not wish to port your overrides, do not include `reuse-values`. Instead, you may choose to pass your existing overrides file by adding `--values custom-values.yaml` to your command during the upgrade.

If you have upgraded YugabyteDB Anywhere to version 2.12 or later and [xCluster replication](../../../explore/going-beyond-sql/asynchronous-replication-ysql/) for your universe was set up via `yb-admin` instead of the UI, follow the instructions provided in [Synchronize replication after upgrade](../upgrade-yp-xcluster-ybadmin/).

## Install Yugabyte Kubernetes Operator by upgrading an existing YBA

The [Yugabyte Kubernetes Operator](../../anywhere-automation/yb-kubernetes-operator/) {{<badge/tp>}} automates the deployment, scaling, and management of YugabyteDB clusters in Kubernetes environments. You can install the operator by upgrading an existing YBA as follows:

1. Apply the following Custom Resource Definition:

    ```sh
    kubectl apply -f https://github.com/yugabyte/charts/blob/2024.1/crds/concatenated_crd.yaml
    ```

1. Get a list of Helm chart releases in namespace using the following command:

    ```sh
    helm ls
    ```

    ```output
    NAME    NAMESPACE       REVISION        UPDATED                                 STATUS          CHART           APP VERSION
    yba         yb-platform-test      1               2024-05-08 16:42:47.480260572 +0000 UTC deployed        yugaware-2.19.3 2.19.3.0-b140
    ```

1. Run the following `helm upgrade` command to enable the YBA upgrade:

    ```sh
    helm upgrade yba yugabytedb/yugaware --version 2024.1.0 --set kubernetesOperatorEnabled=true,kubernetesOperatorNamespace="yb-platform-test"
    ```

1. Verify that YBA is up, and the Kubernetes Operator is installed successfully using the following commands:

    ```sh
    kubectl get pods -n <yba_namespace>
    ```

    ```sh
    kubectl get pods -n <operator_namespace>
    ```

    ```output
    NAME                                       READY   STATUS    RESTARTS   AGE
    chart-1706728534-yugabyte-k8s-operator-0   3/3     Running   0          26h
    ```

    Additionally, you should see no stack traces, but the following messages in the `KubernetesOperatorReconciler` log:

    ```output
    LOG.info("Finished running ybUniverseController");
    ```

1. Create the following custom resource, and save it as `demo-universe.yaml`.

    ```yaml
    # demo-universe.yaml
    apiVersion: operator.yugabyte.io/v1alpha1
    kind: YBUniverse
    metadata:
      name: demo-test
    spec:
      numNodes: 1
      replicationFactor: 1
      enableYSQL: true
      enableNodeToNodeEncrypt: true
      enableClientToNodeEncrypt: true
      enableLoadBalancer: true
      ybSoftwareVersion: "2024.1.0-b2" <- This will be the YBA  version
      enableYSQLAuth: false
      enableYCQL: true
      enableYCQLAuth: false
      gFlags:
        tserverGFlags: {}
        masterGFlags: {}
      deviceInfo:
        volumeSize: 100
        numVolumes: 1
        storageClass: "yb-standard"
    ```

1. Create a universe using the custom resource, `demo-universe.yaml` as follows:

    ```sh
    kubectl apply -f demo-universe.yaml -n yb-platform
    ```

1. Check the status of the universe as follows:

    ```sh
    kubectl get ybuniverse  -n yb-operator
    ```

    ```output
    NAME                STATE   SOFTWARE VERSION
    anab-test-2         Ready   2.23.0.0-b33
    anab-test-backups   Ready   2.21.1.0-b269
    anab-test-restore   Ready   2.21.1.0-b269
    ```

For more information, see [Yugabyte Kubernetes Operator](../../anywhere-automation/yb-kubernetes-operator/).
