---
title: Upgrade YugabyteDB Anywhere installation on Kubernetes
headerTitle: Upgrade YugabyteDB Anywhere
linkTitle: Upgrade installation
description: Upgrade YugabyteDB Anywhere installation on Kubernetes
aliases:
  - /stable/yugabyte-platform/manage-deployments/migrate-to-helm3/
menu:
  stable_yugabyte-platform:
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
    <a href="../upgrade-yp-kubernetes/" class="nav-link active">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>Kubernetes</a>
  </li>

</ul>

You can use [Helm](https://helm.sh/) to upgrade your YugabyteDB Anywhere (YBA) installed on [Kubernetes](https://kubernetes.io/) to a newer version.

If you are upgrading a YugabyteDB Anywhere installation with high availability enabled, follow the instructions provided in [Upgrade instances](../../administer-yugabyte-platform/high-availability/#upgrade-instances).

{{< tip title="Install the YugabyteDB Kubernetes Operator" >}}
To install the [YugabyteDB Kubernetes Operator](../../anywhere-automation/yb-kubernetes-operator/) on an existing YugabyteDB Anywhere instance, see [Installing Kubernetes Operator](../../anywhere-automation/yb-kubernetes-operator/#installing-kubernetes-operator).
{{< /tip >}}

Before running an upgrade, execute the following command to obtain the latest versions of YugabyteDB Anywhere from Helm charts:

```sh
helm repo update
```

## Upgrade using Helm

To upgrade to a specific version while preserving overrides you might have applied to your initial YugabyteDB Anywhere installation or previous upgrades, execute the following command:

```sh
helm upgrade yw-test yugabytedb/yugaware \
  --version {{<yb-version version="stable" format="short">}} \
  -n yb-platform \
  --reset-then-reuse-values \
  --set image.tag={{<yb-version version="stable" format="build">}} \
  --atomic \
  --timeout 30m \
  --wait
```

To obtain the value for `--set image.tag`, execute the following command:

```sh
helm list | awk '{if (NR!=1) print $NF}'
```

If you do not wish to port your overrides, do not include `--reset-then-reuse-values`. Instead, you may choose to pass your existing overrides file by adding `--values custom-values.yaml` to your command during the upgrade.

When `--atomic` is used, if the upgrade fails for any reason (pods not Ready, hooks fail, timeout, and so on), Helm will automatically roll back to the last successful release.

Use `--timeout` to give the upgrade enough time; if the upgrade exceeds this time, Helm treats it as a failure, and (if `--atomic` is set) triggers a rollback.

When `--wait` is used, Helm waits for workloads to become Ready.

If you have upgraded YugabyteDB Anywhere to version 2.12 or later and [xCluster replication](../../../explore/going-beyond-sql/asynchronous-replication-ysql/) for your universe was set up via yb-admin instead of the UI, follow the instructions provided in [Synchronize replication after upgrade](../upgrade-yp-xcluster-ybadmin/).
