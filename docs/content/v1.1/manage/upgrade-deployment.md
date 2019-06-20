---
title: Upgrade Deployment
linkTitle: Upgrade Deployment
description: Upgrade Deployment
menu:
  v1.1:
    identifier: manage-upgrade-deployment
    parent: manage
    weight: 705
isTocNested: true
showAsideToc: true
---

The basic flow is to upgrade each yb-master and yb-tserver one at a time, verifying after each step from the yb-master Admin UI that the cluster is healthy and the upgraded process is back online.

If you plan to script this in a loop, then a pause of about 60 secs is recommended before moving from one process/node to another.

{{<tip title="Preserving Data and Cluster Configuration Across Upgrades" >}}
Your data/log/conf directories are generally stored in a separate location which stays the same across the upgrade so that the cluster data, its configuration settings are retained across the upgrade.
{{< /tip >}}


## Upgrade YB-Masters

```
1. pkill yb-master
2. switch binaries to new release
3. start the yb-master process
4. verify in http://<any-yb-master>:7000/ that all masters are alive
5. pause ~60 secs before upgrading next yb-master
```

## Upgrade YB-TServers

```
1. pkill yb-tserver
2. switch binaries to new release
3. start yb-tserver process
4. verify in http://<any-yb-master>:7000/tablet-servers to see if the new TServer is alive and heart beating
5. pause ~60 secs before upgrading next yb-tserver
```
