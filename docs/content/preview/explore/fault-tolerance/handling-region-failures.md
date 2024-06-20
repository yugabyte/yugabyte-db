---
title: Handling region failures
headerTitle: Handling region failures
linkTitle: Region failures
description: YugabyteDB can handle region failure
headcontent: Be resilient to failures of entire regions
menu:
  preview:
    identifier: handling-region-failures
    parent: fault-tolerance
    weight: 40
type: docs
---

YugabyteDB is resilient to a single-domain failure in a deployment with a replication factor (RF) of 3. To survive region failures, you deploy across multiple regions. Let's see how YugabyteDB survives a region failure.

{{<tip title="RF 3 vs RF 5">}}
Although you could use an RF 3 cluster, an RF 5 cluster provides quicker failover; with two replicas in the preferred regions, when a leader fails, a local follower can be elected as a leader, rather than a follower in a different region.
{{</tip>}}

## Setup

Consider a scenario where you have deployed your database across three regions - us-west, us-east, and us-central. Typically, you choose one region as the [preferred region](../../multi-region-deployments/synchronous-replication-ysql/#preferred-region) for your database. This is the region where your applications are active. Then determine which region is closest to the preferred to be the failover region for your applications, and set this region as your second preferred region for the database. The third region needs no explicit setting and automatically becomes the third preferred region.

<!-- begin: nav tabs -->
{{<nav/tabs list="local,anywhere" active="local"/>}}

{{<nav/panels>}}
{{<nav/panel name="local" active="true">}}
<!-- local cluster setup instructions -->
{{<setup/local numnodes="3" rf="3" locations="aws.us-east.us-east-1a,aws.us-central.us-central-1a,aws.us-west.us-west-1a" fault-domain="region">}}
{{</nav/panel>}}

{{<nav/panel name="anywhere">}} {{<setup/anywhere>}} {{</nav/panel>}}
{{</nav/panels>}}
<!-- end: nav tabs -->

{{<note>}}
All illustrations adhere to the legend outlined in [Legend for illustrations](../../../contribute/docs/docs-layout#legend-for-illustrations)
{{</note>}}

In the following illustration, the leaders are in us-east (the preferred region), which is also where the applications are active. The standby application is in us-central and will be the failover. This has been set as the second preferred region for the database

![Sync replication setup - Handling region outage](/images/explore/fault-tolerance/region-failure-setup.png)

Users reach the application via a router/load balancer which would point to the active application in us-east.

Now when the application writes, the leaders replicate to both the followers in us-west and us-central.

As us-central is closer to us-east than us-west, the followers in central will acknowledge the write faster, so that follower will be up-to-date with the leader in us-east.

## Third region failure

If the third (least preferred) region fails, availability is not affected at all. This is because the leaders will be in the preferred region, and there will be one follower in the second preferred region. There is no data loss and no recovery is needed.

<!-- begin nav tabs -->
{{<nav/tabs list="local,anywhere" active="local"/>}}

{{<nav/panels>}}
{{<nav/panel name="local" active="true">}}
<!-- local cluster setup instructions -->
{{<collapse title="Simulate failure of the third region locally">}}
To simulate the failure of the 3rd region locally, you can just stop the third node.

{{%cluster/cmd op="stop" nodes="3"%}}

{{</collapse>}}
{{</nav/panel>}}

{{<nav/panel name="anywhere">}}
{{<note>}} To stop a node in YB Anywhere, see [YBA - Manage nodes](../../../yugabyte-platform/manage-deployments/remove-nodes/#start-and-stop-node-processes). {{</note>}}
{{</nav/panel>}}

{{</nav/panels>}}
<!-- end nav tabs -->

The following illustration shows the scenario of the third region (us-west) region failing. As us-east has been set as the preferred region, there are no leaders in us-west. When this region fails, availability of the applications is not affected at all as there is still one follower active (in us-central) for replication to continue correctly.

![Third region failure - Handling region outage](/images/explore/fault-tolerance/region-failure-third-region.png)

## Secondary region failure

When the second preferred region fails, availability is not affected at all. This is because there are no leaders in this region - all the leaders are in the preferred region. But your write latency could be affected as every write to the leader has to wait for acknowledgment from the third region, which is farther away. Reads are not affected as the primary application will read from the leaders in the preferred region. There is no data loss at all.

<!-- begin nav tabs -->
{{<nav/tabs list="local,anywhere" active="local"/>}}

{{<nav/panels>}}
{{<nav/panel name="local" active="true">}}
{{<collapse title="Simulate failure of the secondary region locally" >}}
To simulate the failure of the secondary region locally, you can just stop the second node.

{{%cluster/cmd op="stop" nodes="2"%}}

{{</collapse>}}
{{</nav/panel>}}

{{<nav/panel name="anywhere">}}
{{<note>}} To stop a node in YB Anywhere, see [YBA - Manage nodes](../../../yugabyte-platform/manage-deployments/remove-nodes/#start-and-stop-node-processes). {{</note>}}
{{</nav/panel>}}

{{</nav/panels>}}
<!-- end nav tabs -->

In the following illustration, you can see that as us-central has failed, writes are replicated to us-west. This leads to higher write latencies as us-west is farther away from us-east than us-central. But there is no data loss.

![Second region failure - Handling region outage](/images/explore/fault-tolerance/region-failure-second-region.png)

## Preferred region failure

When the preferred region fails, there is no data loss but availability will be affected for a short time. This is because all the leaders are located in this region, and your applications are also active in this region. At the moment of the preferred region failure, all tablets will have no leaders.

<!-- begin: nav tabs -->
{{<nav/tabs list="local,anywhere" active="local"/>}}

{{<nav/panels>}}
{{<nav/panel name="local" active="true">}}
{{<collapse title="Simulate failure of the primary region locally" >}}
To simulate the failure of the primary region locally, you can just stop the first node.

{{%cluster/cmd op="stop" nodes="1"%}}

{{</collapse>}}
{{</nav/panel>}}

{{<nav/panel name="anywhere">}}
{{<note>}} To stop a node in YB Anywhere, see [YBA - Manage nodes](../../../yugabyte-platform/manage-deployments/remove-nodes/#start-and-stop-node-processes). {{</note>}}
{{</nav/panel>}}

{{</nav/panels>}}
<!-- end: nav tabs -->

The following illustration shows the failure of the preferred region, us-east.

![Preferred region failure - Handling region outage](/images/explore/fault-tolerance/region-failure-primary-region.png)

Because there are no leaders for the tablets, leader election is triggered and the followers in the secondary region will be promoted to leaders. Leader election typically takes about 3 seconds.

The following illustration shows that the followers in us-central have been elected as leaders.

![Preferred region failure - Leader election](/images/explore/fault-tolerance/primary-failure-leader-election.png)

You will now have to fail over your applications. As you have already set up standby applications in us-central, you have to make changes in your load balancer to route the user traffic to the secondary region. The following illustration shows the load balancer routing user requests to the application instance in us-central.

![Preferred region failure - Leader election](/images/explore/fault-tolerance/primary-failure-lb-routing.png)

## Smart driver

You can opt to use a YugabyteDB Smart Driver in your applications. The major advantage of this driver is that it is aware of the cluster configuration and will automatically route user requests to the secondary region in case the preferred region fails.

The following illustration shows how the primary application (assuming it is still running) in us-east automatically routes the user requests to the secondary region in the case of preferred region failure without any change needed in the load balancer.

![Preferred region failure - Leader election](/images/explore/fault-tolerance/primary-failure-smart-driver.png)

## Region failure in a two-region setup

There may be scenarios where you want to deploy the database in just one region. It is quite common for enterprises to have one data center as their primary and another data center just for failover. For this scenario, you can deploy YugabyteDB in your primary data center and set up another cluster in the second data center that gets the data from the primary cluster via asynchronous replication. This is also known as the 2DC or xCluster model.

{{<lead link="../../../develop/build-global-apps/active-active-single-master">}}
You can set this up by following the instructions of the [Active-Active Single-Master](../../../develop/build-global-apps/active-active-single-master) pattern.
{{</lead>}}
