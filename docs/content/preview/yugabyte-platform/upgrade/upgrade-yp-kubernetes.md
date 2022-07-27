---
title: Upgrade YugabyteDB Anywhere installation on Kubernetes
headerTitle: Upgrade YugabyteDB Anywhere installation on Kubernetes
linkTitle: Upgrade Kubernetes installation
description: Upgrade YugabyteDB Anywhere installation on Kubernetes
menu:
  preview_yugabyte-platform:
    identifier: upgrade-yp-kubernetes
    parent: upgrade
    weight: 80
type: docs
---

You can use [Helm](https://helm.sh/) to upgrade your YugabyteDB Anywhere installed on [Kubernetes](https://kubernetes.io/) to a newer version.

Before running an upgrade, execute the following command to obtain the latest versions of YugabyteDB Anywhere from Helm charts:

```sh
helm repo update
```

To upgrade to a specific version while preserving overrides you might have applied to your initial YugabyteDB Anywhere installation or previous upgrades, execute the following command:

```sh
helm upgrade yw-test yugabytedb/yugaware --version 2.13.0 -n yb-platform --reuse-values --wait
```

If you do not wish to port your overrides, do not include `reuse-values`. Instead, you may choose to pass your existing overrides file by adding `--values custom-values.yaml` to your command during the upgrade.

{{< note title="Note" >}}

If you set up xCluster replication using yb-admin (instead of via the Platform UI) and upgrade Platform to 2.12+, you need to synchronize xCluster replication by running the [Sync xcluster config](https://api-docs.yugabyte.com/docs/yugabyte-platform/e19b528a55430-sync-xcluster-config) API call after upgrading. Refer to [Synchronize xCluster replication](../upgrade-yp-replicated/#synchronize-xcluster-replication).

{{< /note >}}
