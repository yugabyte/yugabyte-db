---
title: Use Yugabyte Platform to add a node
headerTitle: Add a node
linkTitle: Add a node
description: Use Yugabyte Platform to add a node.
aliases:
  - /latest/manage/enterprise-edition/create-universe-multi-region
menu:
  latest:
    identifier: add-nodes
    parent: manage-deployments
    weight: 20
isTocNested: true
showAsideToc: true
---

You can revive a node on a new backing instance by using the Yugabyte Platform UI, as follows:

- Navigate to **Universes**.

- Select your universe. 

- Open the **Nodes** tab.

- Find a node with a Decommissioned status and click its corresponding **Actions > Add Node**, as per the following illustration:<br><br>

  ![Add Node Actions](/images/ee/node-actions-add-node.png)

<br> 

For Infrastructure as a service (IaaS) such as AWS and GCP, Yugabyte Platform will spawn with the existing node instance type in the existing region and zone of that node. When the process completes, the node will have the Master and TServer processes running, along with data that is load-balanced onto this node. The node's status will be shown as Live. The node's name is reused.

