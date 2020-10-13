---
title: Common error messages
linkTitle: Common error messages
headerTitle: Understanding common error messages
description: How to understand and recover from common error messages
menu:
  latest:
    parent: troubleshoot
    weight: 800
isTocNested: true
showAsideToc: true
---

## Skipping add replicas

When a new node has joined the cluster or an existing node has been removed, you may see errors messages like below:

```
W1001 10:23:00.969424 22338 cluster_balance.cc:232] Skipping add replicas for 21d0a966e9c048978e35fad3cee31698: 
Operation failed. Try again. Cannot add replicas. Currently have a total overreplication of 1, when max allowed is 1
```

This message is harmless. It means that the maximum number of concurrent tablets being remote bootstrapped across the
 cluster by the yb-master load balancer has reached it's limit. 
 This limit is configured in `--load_balancer_max_concurrent_tablet_remote_bootstraps` in 
 [yb-master config](../../../reference/configuration/yb-master#load-balancer-max-concurrent-tablet-remote-bootstraps).
