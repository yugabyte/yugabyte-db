---
title: Follower Reads for global applications
headerTitle: Follower Reads
linkTitle: Follower Reads
description: Reducing Read Latency for global applications
headcontent: Reducing Read Latency for global applications
menu:
  preview:
    identifier: global-apps-follower-reads
    parent: build-global-apps
    weight: 800
rightNav:
  hideH3: true
  hideH4: true
type: docs
---

For a highly available system, it is typical to opt for a [Global database](../global-database) that spans multiple regions with preferred leaders set to a specific region. This is great for apps that are running that region as all the reads would go to the leader located locally.

But apps running in other regions would have to incur the cross-region latency to read the latest data from the leader even though there is a follower present locally. This is because all followers may not have the latest data at the read time.

There are a few scenarios where reading from the leader is not necessary. For example,

- The data does not change often (eg. movie database)
- The application does not need the latest data (eg. yesterday’s report)

If a little staleness for reads is okay for the app running in the other regions, then **Follower Reads** is the pattern to adopt. Let's look into how this can be beneficial for your application.

## Overview

{{<cluster-setup-tabs>}}

Let's say you have a [Global Database](./global-database) set up across 3 regions `us-east`, `us-central`, `us-west` with leader preference set to `us-east`. Let's say that you want to run apps in all the 3 regions. Then the read latencies would be similar to the following illustration.

![Global Apps - setup](/images/develop/global-apps/global-apps-follower-reads-setup.png)

## Follower reads

Enable follower reads for your application with these statements.

```plpgsql
SET session characteristics as transaction read only;
SET yb_read_from_followers = true;
```

This will enable the application to read data from the closest follower (or leader). 

![Follower reads - setup](/images/develop/global-apps/global-apps-follower-reads-final.png)

Notice that the read latency for the app in `us-west` has drastically dropped to `2ms` from the initial `60ms` and the read latency of the app in `us-central` has also dropped to `2ms`.

As replicas may not be up-to-date with all updates, by design, this might return slightly stale data (default: 30s). This is the case even if the read goes to a leader. The staleness value can be changed via another setting like:

```plpgsql
SET yb_follower_read_staleness_ms = 10000; -- 10s
```

{{<note>}}
This is only for reads. All writes still go to the leader.
{{</note>}}

## Failover

When the follower in a region fails, the app will redirect its read to the next closest follower/leader.

![Follower reads - Failover](/images/develop/global-apps/global-apps-follower-reads-failover.png)

Notice how the app in `us-west` reads from the follower in `us-central` when the follower in `us-west` has failed. Even now, the read latency is just `40ms`, much lesser than the original `60ms`.