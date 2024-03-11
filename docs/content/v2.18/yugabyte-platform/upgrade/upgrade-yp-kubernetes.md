---
title: Upgrade YugabyteDB Anywhere installation on Kubernetes
headerTitle: Upgrade YugabyteDB Anywhere
linkTitle: Upgrade installation
description: Upgrade YugabyteDB Anywhere installation on Kubernetes
menu:
  v2.18_yugabyte-platform:
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

You can use [Helm](https://helm.sh/) to upgrade your YugabyteDB Anywhere installed on [Kubernetes](https://kubernetes.io/) to a newer version.

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

If you have upgraded YugabyteDB Anywhere to version 2.12 or later and [xCluster replication](../../../explore/multi-region-deployments/asynchronous-replication-ysql/) for your universe was set up via `yb-admin` instead of the UI, follow the instructions provided in [Synchronize replication after upgrade](../upgrade-yp-xcluster-ybadmin/).
