---
title: Replace failed master
linkTitle: Replace failed master
description: Replace failed master
aliases:
  - /troubleshoot/cluster/replace-master/
  - /latest/troubleshoot/cluster/replace-master/
menu:
  latest:
    parent: troubleshoot-cluster
    weight: 831
isTocNested: true
showAsideToc: true
---

Suppose you have a cluster running and now a yb-master has failed and needs to be replaced. The procedure to follow in this 
scenario is:

{{< note title="Note" >}}
Assumptions:
- An initial set of masters M1, M2, M3
- We are removing M1
- We want to add M4
- We'll be using the default master RPC port of 7100
{{< /note >}}



## Start new master and add to cluster
Start `M4` with `--master_addresses= (empty string)`, this will put the master in what we refer to as `ShellMode`.

Add the new master into the quorum using:
```bash
./yb-admin -master_addresses M1:7100,M2:7100,M3:7100 change_master_config ADD_SERVER M4 7100
```

## Remove old master
Remove the old master from the quorum using:
```bash
./yb-admin -master_addresses M1:7100,M2:7100,M3:7100,M4:7100 change_master_config REMOVE_SERVER M1 7100
```

Note: the master addresses needs to include all 4 masters, in case the new one is suddenly the master leader!)

## Validate cluster

Validate that your set of masters is now `M2`, `M3` and `M4` using:
```bash
yb-admin -master_addresses M2:7100,M3:7100,M4:7100 list_all_masters
```

Until [#1542](https://github.com/yugabyte/yugabyte-db/issues/1542) is implemented, the TS will by default only know of 
whatever masters are encoded in the `--tserver_master_addrs` flag that they are started with. 

If any one of those masters is still part of the active quorum, then they will propagate the new master quorum over via heartbeats. 
If, however, none of the current masters are present in the TS flag, then the TS will not be able to join the cluster! 

So it is important to make sure to update `--tserver_master_addrs` on every TS to the new set of master addresses, `M2:7100,M3:7100,M4:7100`!

If the master you wish to replace is already dead (eg: VM was terminated), you might want to first do the `REMOVE` step, then do the `ADD` step.
