---
title: Follower Reads for global applications
headerTitle: Follower reads
linkTitle: Follower reads
description: Reducing Read Latency for global applications
headcontent: Reduce read latency for global applications
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


When applications are running in multiple regions, they incur cross-region latency to read the latest data from the leader, even though there could be a follower present locally. This is because all followers may not have the latest data at the read time.

There are a few scenarios where reading from the leader is not necessary. For example:

- The data does not change often (for example, movie database).
- The application does not need the latest data (for example, yesterday's report).

If a little staleness for reads is okay for the application running in the other regions, then **Follower Reads** is the pattern to adopt. Let's look into how this can be beneficial for your application.

{{<tip>}}
Multiple application instances are active and some instances read stale data.
{{</tip>}}

## Overview

{{<cluster-setup-tabs>}}

Suppose you have a [Global Database](../global-database) set up across 3 regions `us-east`, `us-central`, and `us-west`, with leader preference set to `us-east`. Suppose further that you want to run applications in all the 3 regions. Then the read latencies would be similar to the following illustration.

![Global Apps - setup](/images/develop/global-apps/global-apps-follower-reads-setup.png)

## Follower reads

Enable follower reads for your application using the following statements:

```plpgsql
SET session characteristics as transaction read only;
SET yb_read_from_followers = true;
```

This allows the application to read data from the closest follower (or leader).

![Follower reads - setup](/images/develop/global-apps/global-apps-follower-reads-final.png)

In this scenario, the read latency for the application in `us-west` drops drastically to 2 ms from the initial 60 ms, and the read latency of the application in `us-central` also drops to 2 ms.

As replicas may not be up-to-date with all updates, by design, this might return slightly stale data (the default staleness is 30 seconds). This is the case even if the read goes to a leader. The staleness value can be changed using the following setting:

```plpgsql
SET yb_follower_read_staleness_ms = 10000; -- 10s
```

{{<note>}}
This is only for reads. All writes still go to the leader.
{{</note>}}

## Failover

When the follower in a region fails, the application redirects its reads to the next closest follower/leader.

![Follower reads - Failover](/images/develop/global-apps/global-apps-follower-reads-failover.png)

Notice how the application in `us-west` reads from the follower in `us-central` when the follower in `us-west` has failed. Even now, the read latency is just 40 ms, much less than the original 60 ms.

## Learn more

- [Follower reads](../../../explore/ysql-language-features/going-beyond-sql/follower-reads-ysql/)
