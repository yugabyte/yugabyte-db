---
title: Global Database for global applications
headerTitle: Global Database
linkTitle: Global Database
description: Build highly available global applications
headcontent: Design highly available global applications with a Global Database
image: /images/section_icons/quick_start/sample_apps.png
menu:
  preview:
    identifier: global-apps-global-database
    parent: build-global-apps
    weight: 210
rightNav:
  hideH3: true
  hideH4: true
type: docs
---

For most applications, a single-region multi-zone deployment would suffice. But global applications that are designed to serve users across multiple geographies and be highly available have to be deployed in multiple regions.

To be ready for region failures and be highly available, you can set up YugabyteDB as a cluster that spans multiple regions. This stretch cluster is known as a **Global Database**. Let’s look into how to set this up and understand the benefits of building applications on top of this pattern.

## Overview

Let's say that you want to have your cluster be distributed across three regions: `us-east`, `us-central`, and `us-west` and that you are going to run your app in `us-east` with failover set to `us-central`. For this, you have to set up an `RF5` with 2 copies of the data in each of these regions and the last copy in the third region.

Although you can have an **RF3** cluster, you should set it up as an **RF5** cluster so that you will have 2 copies within the preferred regions. When a leader fails, this would allow a local follower to be elected as a leader rather than a follower in a different region.

{{<cluster-setup-tabs>}}

{{<warning title="For local cluster">}}
This example uses an RF5 cluster setup. When setting up a local cluster make sure you start 5 instances.
{{</warning>}}

You would get a cluster as shown in the following illustration.

![Global Database - Replicas](/images/develop/global-apps/global-database-replicas.png)

## Preferred Regions

As the app is going to be running in `us-east` and you want it to failover to `us-central`, let's configure the database in the same manner. Set `us-east` to be preferred region 1 and `us-central` to be preferred region 2.

```shell
yb-admin set_preferred_zones aws.us-east.*:1 aws.us-central.*:2
```

Now automatically the leaders are placed in `us-east`.

![Global Database - Preferred Leaders](/images/develop/global-apps/global-database-preferred-leaders.png)

## Initial deploy

When the app starts up in the east, it has a very low read latency of `2ms` as it has to read only from leaders which are in the same region. The writes take about `30ms` as every write has to be replicated to at least 2 other replicas, one of which is located within the region and the next closest one is in `us-central` which is about `30ms` away.

![Global Database - App deploy](/images/develop/global-apps/global-database-app.png)

## Failover

The **Global database** is automatically resilient to a single region failure. When a region fails, followers in other regions are promoted to leaders within seconds and will continue to serve requests without any data loss. This is because the raft-based **synchronous replication** guarantees that at least `1 + RF/2` (`RF` = replication factor) nodes are consistent and up-to-date with the latest data. This enables the newly elected leader to serve the latest data immediately without any downtime for your users.

![Global Database - App Failover](/images/develop/global-apps/global-database-failover.png)

As we had set up `us-central` to be the second preferred region, when `us-east` fails, the followers in `us-central` are automatically elected to be the leaders. The app also starts communicating with the leaders in `us-central` as we had configured `us-central` to be the first failover region. Notice that the write latency has increased to `40ms` from `30ms`. This is because the first replica is right in `us-central` along with the leader, but the second replica is in `us-west` which is `40ms` away.

## Improve latencies with closer regions

You can reduce the write latencies further by opting to deploy the cluster across regions that are closer to each other. For instance, instead of choosing `us-east`, `us-central`, and `us-west` which are `30-60 ms` away from each other, we could choose to deploy the cluster across `us-east-1`, `us-central`, and `us-east-2`, which are just `10-40 ms` away.

![Global Database - App Failover](/images/develop/global-apps/global-database-closer-regions.png)

This would drastically reduce the write latency to `10ms` from the initial `30ms`.

## Learn more

- [Raft consensus protocol](../../../architecture/docdb-replication/replication)
- [yb-admin set-preferred-zones](../../../admin/yb-admin/#set-preferred-zones)
