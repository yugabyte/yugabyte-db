---
title: Upgrade YugabyteDB Anywhere installation on Kubernetes
headerTitle: Upgrade YugabyteDB Anywhere installation on Kubernetes
linkTitle: Upgrade Kubernetes installation
description: Upgrade YugabyteDB Anywhere installation on Kubernetes
menu:
  preview:
    identifier: upgrade-yp-kubernetes
    parent: upgrade
    weight: 80
isTocNested: true
showAsideToc: true
---

You can use [Helm](https://helm.sh/) to upgrade your YugabyteDB Anywhere installed on [Kubernetes](https://kubernetes.io/) to a newer version.

To perform an upgrade to a specific version while preserving overrides you might have applied to your initial YugabyteDB Anywhere installation or previous upgrades, execute the following command:

```sh
helm upgrade yw-test yugabytedb/yugaware --version 2.13.0 -n yb-platform --reuse-values --wait
```

If you do not wish to port your overrides, do not include `reuse-values`. Instead, you may choose to pass your existing overrides file by adding `--values custom-values.yaml` to your command during the upgrade.