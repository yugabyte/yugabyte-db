---
title: Use Yugabyte Platform to add a node
headerTitle: Use Yugabyte Platform to add a node
linkTitle: Add a node
description: Use Yugabyte Platform to add a node.
aliases:
  - /latest/manage/enterprise-edition/create-universe-multi-region
menu:
  latest:
    identifier: add-node-yp
    parent: manage-deployments-yugabyte-platform
    weight: 40
isTocNested: true
showAsideToc: true
---

### Add node

The node can brought back to life on a new backing instance using the **Add Node** option from the dropdown for the `Decomissioned` node. For IaaS, like AWS and GCP, the Yugabyte Platform application will spawn with the existing instance type in the correct/existing region and zone of that node. After the end of this operation, the node will have `yb-master` and `yb-tserver` processes running along with some data that is load balanced onto this node and status will be marked `Live`. Note that the node name is reused and is part of the healthy cluster now.

![Add Node Actions](/images/ee/node-actions-add-node.png)
