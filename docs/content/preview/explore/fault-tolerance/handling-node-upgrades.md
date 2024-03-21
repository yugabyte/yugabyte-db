---
title: Handling node upgrades
headerTitle: Handling node upgrades
linkTitle: Periodic maintenance
description: No service disruption during node upgrades and periodic maintenance
headcontent: No service disruption during node upgrades and periodic maintenance
menu:
  preview:
    identifier: handling-node-upgrades
    parent: fault-tolerance
    weight: 50
type: docs
---

There are many scenarios where you have to do planned maintenance on your cluster. These could be software upgrades, upgrading your machines or applying security patches that need restart. When doing upgrades it is common to do it in a rolling fashion (a.k.a Rolling upgrades) wherein one node is taken offline, upgraded and added back into the system. Let us see how YugabyteDB is resilient to such events and continues any service interruption.

## Setup

Consider a setup where YugabyteDB is deployed in a single region(us-east-1) across 3 zones. Say it is an RF3 cluster with leaders and followers distributed across the 3 zones (a,b,c) with 6 nodes 1,2,3,4,5 & 6.

<!-- begin: nav tabs -->
{{<nav/tabs list="local,anywhere,cloud" active="local"/>}}

{{<nav/panels>}}
{{<nav/panel name="local" active="true">}}
<!-- local cluster setup instructions -->
{{<collapse title="Setup a local cluster">}}
{{<setup/local
  numnodes="6"
  rf="3"
  locations="aws.us-east.us-east-1a,aws.us-east.us-east-1a,aws.us-east.us-east-1b,aws.us-east.us-east-1b,aws.us-east.us-east-1c,aws.us-east.us-east-1c"
  fault-domain="zone">}}
{{</collapse>}}
{{</nav/panel>}}

{{<nav/panel name="anywhere">}} {{<setup/anywhere>}} {{</nav/panel>}}
{{<nav/panel name="cloud">}} {{<setup/cloud>}} {{</nav/panel>}}
{{</nav/panels>}}
<!-- end: nav tabs -->

The application typically connects to all the nodes in the cluster as shown in the following illustration.

{{<note>}}
In all the illustrations on this page, the solid circles are tablet leaders and the dotted circles are followers.
{{</note>}}

![Single region, 3 zones, 6 nodes](/images/explore/fault-tolerance/node-upgrades-setup.png)

## Upgrading a node

When upgrading a node, the first step is taking it offline. But there are a few actions that need to be taken before it is taken offline.

<!-- begin nav tabs -->
{{<nav/tabs list="local,anywhere,cloud" active="local"/>}}

{{<nav/panels>}}
{{<nav/panel name="local" active="true">}}
<!-- local cluster setup instructions -->
{{<collapse title="Take a node offline locally">}}
To simulate taking a node offline locally, you can just stop the fourth node.

```bash
./bin/yugabyted stop --base_dir=/tmp/ybd4
```

{{</collapse>}}
{{</nav/panel>}}

{{<nav/panel name="anywhere">}}
{{<note>}} To stop a node in YB Anywhere, see [YBA - Manage nodes](../../../yugabyte-platform/manage-deployments/remove-nodes/#start-and-stop-node-processes) {{</note>}}
{{</nav/panel>}}

{{<nav/panel name="cloud">}}
{{<note>}} Please reach out [YugabyteDB support](https://support.yugabyte.com) to stop a node in [YB Managed](../../../yugabyte-cloud/) for an upgrade {{</note>}}
{{</nav/panel>}}

{{</nav/panels>}}

In the illustration below, we have chosen node 4 to be upgraded.

![Upgrade a single node](/images/explore/fault-tolerance/node-upgrades-take-offline.png)

## Leaders move

There could be leaders on the node to be upgraded & they have to be first moved out so that there is no service disruption. For this, an explicit leader election is triggered with a hint to choose a new leader outside the zone where the node is located. This will be repeated for all the leaders on that node. Note, that even though the followers in this node will soon go offline, writes won't be affected as there will be followers located in other zones.

For example, in the illustration below, the follower for tablet-4 in node-2 located in zone-a has been elected as the new leader and hence the replica of tablet 4 in node 4 has been downgraded to follower.

![Leader movement](/images/explore/fault-tolerance/node-upgrades-leader-move.png)

## Node offline

Once the leaders are moved out of the node, it can be taken offline. During this process, connections that have already been established to the node will start timing out. The default TCP timeout is about 15s. New connections cannot be established. once the node is offline, you can apply new software or upgrade the hardware. There is no service disruption during this period as all the tablets have active leaders.

![Take node offline](/images/explore/fault-tolerance/node-upgrades-node-offline.png)

## Node online

Once the upgrade and the needed maintenance are complete, once the node is restarted, it is automatically added back into the cluster. The cluster will notice that the leaders and followers are unbalanced across the cluster and will trigger a rebalance and leader election. This will ensure that the leaders and followers are evenly distributed across the cluster. All the nodes in the cluster are fully functional and can start taking in load. There is neither data loss nor service disruption during the entire time.

<!-- begin nav tabs -->
{{<nav/tabs list="local,anywhere,cloud" active="local"/>}}

{{<nav/panels>}}
{{<nav/panel name="local" active="true">}}
{{<collapse title="Bring back a node online locally">}}
To simulate bring back a node online locally, you can just start the stopped node.

```bash
./bin/yugabyted start --base_dir=/tmp/ybd4
```

{{</collapse>}}
{{</nav/panel>}}

{{<nav/panel name="anywhere">}}
{{<note>}} To restart a node in YB Anywhere, see [YBA - Manage nodes](../../../yugabyte-platform/manage-deployments/remove-nodes/#start-and-stop-node-processes) {{</note>}}
{{</nav/panel>}}

{{<nav/panel name="cloud">}}
{{<note>}} Please reach out [YugabyteDB support](https://support.yugabyte.com) to restart a node in [YB Managed](../../../yugabyte-cloud/)  {{</note>}}
{{</nav/panel>}}

{{</nav/panels>}}

Notice in the illustration that the tablet followers in node-4 are updated with the latest data and made leaders.

![Back online](/images/explore/fault-tolerance/node-upgrades-back-online.png)
