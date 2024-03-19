---
title: Handling region failures
headerTitle: Handling region failures
linkTitle: HA during region failures
description: YugabyteDB can handle region failure
headcontent: Be resilient to failures of entire regions
menu:
  preview:
    identifier: handling-region-failures
    parent: fault-tolerance
    weight: 10
type: docs
---

YugabyteDB is resilient to a single-region failure in an RF3 cluster. It is mandatory to deploy across multiple regions to survive region failures. In this section let us see how YugabyteDB is resilient to a region failure.

## Setup

Consider a scenario where you have deployed your database across 3 regions - us-west,us-east and us-central. You have to choose one region as your primary region for your database. This is typically the region where your applications are active. Then decide a closer region to be the failover region for your applications. You have to set this region as your second preferred region for the database. The third region needs no explicit setting and automatically becomes the third preferred region.

In the following illustration, the leaders are in us-east as that is the primary region and applications are active here. The standby app is in us-central and will be the failover region for the application. This has been set as the second preferred region for the database.

![Sync replication setup - Handling region outage
](/images/explore/fault-tolerance/region-failure-setup.png)

Users reach the application via a router/load balancer which would point to the active application in us-east.
Now when the application writes, the leaders replicate to both the followers in us-west and us-central.
As us-central is closer to us-east than us-west, the followers in central will acknowledge the write faster, so that follower will be up-to-date with the leader in us-east.


## Third region failure

When the third region(the third or the least preferred region) fails, availability is not affected at all. This is because the leaders will be in the preferred region 1 and there will be one follower in the second preferred region. There is no data loss and there is no recovery needed.

The following illustration shows the scenario of the third region (us-west) region failing. As us-east has been set as the first preferred region, there are no leaders in us-west. When this region fails, availability of the applications is not affected at all as there is still one follower active (in us-central) for replication to continue correctly.

![Third region failure - Handling region outage
](/images/explore/fault-tolerance/region-failure-third-region.png)

## Secondary region failure

When the second preferred region fails, availability is not affected at all. This is because there are no leaders in the second preferred region as all the leaders are in the primary region. But your write latency could be affected as every write to the leader has to wait for the acknowledgment from the third region which could be far away and could lead to increase in write latency. Reads are not affected as the primary application will read from the leaders in the primary region. There is no data loss at all.

In the following illustration, you can see that as the us-central has failed, writes are replicated to us-west. This will lead to higher write latencies as us-west is farther away from us-east than us-central. But there is no data loss.

![Second region failure - Handling region outage
](/images/explore/fault-tolerance/region-failure-second-region.png)

## Primary region failure

When the primary region fails, there is no data loss but availability will be affected for a short time. This is because all the leaders are located here and your applications are active here in that region. At the moment of the primary region failure, all tablets will have no leaders.

The following illustration shows the failure of the primary region, us-east.

![Primary region failure - Handling region outage
](/images/explore/fault-tolerance/region-failure-primary-region.png)

Now as there are no leaders for the tablets, leader election will be triggered and the followers in the secondary region will be promoted to leaders. Leader election typically takes about 3s. The following illustration shows that the followers in us-central have been elected as leaders.

![Primary region failure - Leader election
](/images/explore/fault-tolerance/primary-failure-leader-election.png)

You will now have to failover your apps. As you have already set up standby apps in us-central, you have to make changes in your load balancer to route the user traffic to the secondary region. The following illustration shows the load balancer routing user requests to the application instance in us-central.

![Primary region failure - Leader election
](/images/explore/fault-tolerance/primary-failure-lb-routing.png)

## Smart driver

You can opt to use YugabyteDB Smart Driver in your applications. The major advantage of this driver is that it is well aware of the cluster configuration and will automatically route user requests to the secondary region in case the primary region fails.

The following illustration shows how the primary application (assuming it is still running) in us-east automatically routes the user requests to the secondary region in the case of the primary region failure without any change needed in the load balancer.

![Primary region failure - Leader election
](/images/explore/fault-tolerance/primary-failure-smart-driver.png)

## Region failure in a 2 region setup

There may be scenarios where you want to deploy the database in just one region. It is quite common for enterprises to have 1 datacenter as their primary and another datacenter just for failover. For this scenario, you can deploy YugabyteDB in your primary datacenter and set up another cluster in the second datacenter that gets the data from the primary cluster via async replication. This is also known as the 2DC model.

{{<tip>}}
You can set this up by following the instructions of the [Active-Active Single-Master](../../../develop/build-global-apps/active-active-single-master) pattern.
{{</tip>}}
