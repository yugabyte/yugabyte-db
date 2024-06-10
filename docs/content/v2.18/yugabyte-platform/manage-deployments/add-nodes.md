---
title: Use YugabyteDB Anywhere to recover a node
headerTitle: Recover a node
linkTitle: Recover a node
description: Use YugabyteDB Anywhere to recover a decommissioned node.
menu:
  v2.18_yugabyte-platform:
    identifier: add-nodes
    parent: manage-deployments
    weight: 30
type: docs
---

In some cases, depending on the node's status, YugabyteDB Anywhere allows you to recover a removed node on a new backing instance, as follows:

- Navigate to **Universes**.

- Select your universe.

- Open the **Nodes** tab.

- Find a node with a Decommissioned status and click its corresponding **Actions > Add Node**, as per the following illustration:<br>

  ![Add Node Actions](/images/ee/node-actions-add-node.png)

For Infrastructure as a service (IaaS) such as AWS and GCP, YugabyteDB Anywhere will spawn with the existing node instance type in the existing region and zone of that node. When the process completes, the node will have the Master and TServer processes running, along with data that is load-balanced onto this node. The node's name will be reused and the status will be shown as Live.

For information on removing and eliminating nodes, see [Eliminate an unresponsive node](../remove-nodes/).
