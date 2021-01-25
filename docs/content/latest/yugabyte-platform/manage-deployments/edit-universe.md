---
title: Use Yugabyte Platform to edit a universe
headerTitle: Edit a universe
linkTitle: Edit a universe
description: Use Yugabyte Platform to edit a universe.
aliases:
  - /latest/manage/enterprise-edition/edit-universe/
menu:
  latest:
    identifier: edit-universe
    parent: manage-deployments
    weight: 53
isTocNested: true
showAsideToc: true
---

Expanding a universe to add more nodes as well as shrinking the universe to lesser number of nodes is done by clicking **Edit** on the **Universe** page and then providing the new user intent for the universe. The new user intent can even be for an entirely new configuration of nodes powered by a different instance type. The Yugabyte Platform console orchestrates this change through the YB-Masters powering this universe. These YB-Masters ensure that the new nodes start hosting the tablet leaders for a set of tablets in such a way that the tablet leader count remains evenly balanced across all the available nodes. This background data replication is undertaken in a throttled manner so that the foreground applications are never impacted.

![Edit universe](/images/ee/edit-univ.png)

For information on how to expand a universe created with an on-premise cloud provider and secured with third-party certificates obtained from external CAs, see [How to Expand the Universe](../../security/enable-encryption-in-transit#how-to-expand-the-universe) .

