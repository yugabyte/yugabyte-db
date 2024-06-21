---
title: Handling node upgrades
headerTitle: Handling node upgrades
linkTitle: Periodic maintenance
description: No service disruption during node upgrades and periodic maintenance
headcontent: Be resilient to service disruption during node upgrades
menu:
  v2.20:
    identifier: handling-node-upgrades
    parent: fault-tolerance
    weight: 50
type: docs
---

There are many scenarios where you have to do planned maintenance on your cluster. These could be software upgrades, upgrading your machines, or applying security patches that need restart. YugabyteDB performs rolling upgrades, where nodes are taken offline one at a time, upgraded, and restarted, with zero downtime for the universe as a whole.

Let's see how YugabyteDB is resilient during planned maintenance, continuing without any service interruption.

## Setup

Consider a setup where YugabyteDB is deployed in a single region (us-east-1) across 3 zones, with leaders and followers distributed across the 3 zones (a,b,c) with 6 nodes 1-6.

<!-- begin: nav tabs -->
{{<nav/tabs list="local,anywhere,cloud" active="local"/>}}

{{<nav/panels>}}
{{<nav/panel name="local" active="true">}}
<!-- local cluster setup instructions -->
{{<collapse title="Set up a local cluster">}}
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
In all the following illustrations, the solid circles are tablet leaders and the dotted circles are followers.
{{</note>}}

![Single region, 3 zones, 6 nodes](/images/explore/fault-tolerance/node-upgrades-setup.png)

## Upgrading a node

When upgrading a node or performing maintenance, the first step is to take it offline.

<!-- begin nav tabs -->
{{<nav/tabs list="local,anywhere,cloud" active="local"/>}}

{{<nav/panels>}}
{{<nav/panel name="local" active="true">}}
<!-- local cluster setup instructions -->
{{<collapse title="Take a node offline locally">}}
To take a node offline locally, you can just stop the node.

```bash
./bin/yugabyted stop --base_dir=/tmp/ybd4
```

{{</collapse>}}
{{</nav/panel>}}

{{<nav/panel name="anywhere">}}
{{<note>}} To stop a node in YugabyteDB Anywhere, see [Manage nodes](../../../yugabyte-platform/manage-deployments/remove-nodes/#start-and-stop-node-processes). {{</note>}}
{{</nav/panel>}}

{{<nav/panel name="cloud">}}
{{<note>}} Reach out [YugabyteDB support](https://support.yugabyte.com) to stop a node in [YugabyteDB Managed](/preview/yugabyte-cloud/) for an upgrade. {{</note>}}
{{</nav/panel>}}

{{</nav/panels>}}

In the following illustration, we have chosen node 4 to be upgraded.

![Upgrade a single node](/images/explore/fault-tolerance/node-upgrades-take-offline.png)

### Leaders move

If there are leaders on the node to be upgraded, they must first be moved so that there is no service disruption. Stopping the node automatically triggers a leader election with a hint to choose a new leader outside the zone where the node is located. This is repeated for all the leaders on the node. Note that, even though the followers in this node will soon go offline, writes won't be affected as there are followers located in other zones.

In the following illustration, the follower for tablet-4 in node-2 located in zone-a has been elected as the new leader, and the replica of tablet-4 in node-4 has been downgraded to follower.

![Leader movement](/images/explore/fault-tolerance/node-upgrades-leader-move.png)

### Node goes offline

After the leaders are moved out of the node, YugabyteDB takes the node offline. Connections that have already been established to the node start timing out (as the default TCP timeout is about 15s). New connections also cannot be established.

![Take node offline](/images/explore/fault-tolerance/node-upgrades-node-offline.png)

At this point, you can perform your maintenance, add new software, or upgrade the hardware. There is no service disruption during this period as all the tablets have active leaders.

### Bring the node online

After completing the upgrade and the required maintenance, you restart the node.

<!-- begin nav tabs -->
{{<nav/tabs list="local,anywhere,cloud" active="local"/>}}

{{<nav/panels>}}
{{<nav/panel name="local" active="true">}}
{{<collapse title="Bring back a node online locally">}}

To simulate bringing back a node online locally, you can just start the stopped node.

```bash
./bin/yugabyted start --base_dir=/tmp/ybd4
```

{{</collapse>}}
{{</nav/panel>}}

{{<nav/panel name="anywhere">}}
{{<note>}} To restart a node in YugabyteDB Anywhere, see [Manage nodes](../../../yugabyte-platform/manage-deployments/remove-nodes/#start-and-stop-node-processes). {{</note>}}
{{</nav/panel>}}

{{<nav/panel name="cloud">}}
{{<note>}} Reach out [YugabyteDB support](https://support.yugabyte.com) to restart a node in [YugabyteDB Managed](/preview/yugabyte-cloud/).  {{</note>}}
{{</nav/panel>}}

{{</nav/panels>}}

The node is automatically added back into the cluster. The cluster will notice that the leaders and followers are unbalanced across the cluster, and trigger a re-balance and leader election. This ensures that the leaders and followers are evenly distributed. All the nodes in the cluster are fully functional and can start taking in load.

Notice in the following illustration that the tablet followers in node-4 are updated with the latest data and are made leaders.

![Back online](/images/explore/fault-tolerance/node-upgrades-back-online.png)

During this entire process, there is neither data loss nor service disruption.
