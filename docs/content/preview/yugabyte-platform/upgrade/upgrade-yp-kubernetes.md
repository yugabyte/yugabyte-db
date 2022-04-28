---
title: Upgrade YugabyteDB Anywhere using Kubernetes
headerTitle: Upgrade YugabyteDB Anywhere using Kubernetes
linkTitle: Upgrade using Kubernetes
description: Use Kubernetes to upgrade YugabyteDB Anywhere
menu:
  preview:
    identifier: upgrade-yp-kubernetes
    parent: upgrade
    weight: 80
isTocNested: true
showAsideToc: true
---

You can use [Kubernetes](https://kubernetes.io/) to upgrade your YugabyteDB Anywhere to a newer version.

To perform an upgrade to a specific version while preserving overrides you might have applied to your initial YugabyteDB Anywhere installation or previous upgrades, execute the following command:

```sh
helm upgrade yw-test yugabytedb/yugaware --version 2.13.0 -n yb-platform --reuse-values --wait
```

If you do not wish to port your overrides, do not include `reuse-values`.