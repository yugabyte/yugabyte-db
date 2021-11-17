---
title: Use Yugabyte Platform to edit a universe
headerTitle: Edit a universe
linkTitle: Edit a universe
description: Use Yugabyte Platform to edit a universe.
menu:
  stable:
    identifier: edit-universe
    parent: manage-deployments
    weight: 60
isTocNested: true
showAsideToc: true
---


Yugabyte Platform allows you to expand a universe to add more nodes and shrink the universe to reduce the number of nodes. Typically, you do this by navigating to **Universes > Edit Universe**, as shown in the following illustration.

![Edit universe](/images/ee/edit-univ.png)

Using the **Edit Universe** page, you can specify the new user intent for the universe. This may include a new configuration of nodes powered by a different instance type. 

The Yugabyte Platform console performs these modifications through the YB-Masters powering the universe. The YB-Masters ensure that the new nodes start hosting the tablet leaders for a set of tablets in such a way that the tablet leader count remains evenly balanced across all the available nodes.

Expansion of universes created with an on-premise cloud provider and secured with third-party certificates obtained from external CAs follows a different workflow. For details, see [How to Expand the Universe](../../security/enable-encryption-in-transit#how-to-expand-the-universe) .
