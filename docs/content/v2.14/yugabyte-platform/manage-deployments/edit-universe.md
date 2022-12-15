---
title: Use YugabyteDB Anywhere to edit a universe
headerTitle: Edit a universe
linkTitle: Edit a universe
description: Use YugabyteDB Anywhere to edit a universe.
menu:
  v2.14_yugabyte-platform:
    identifier: edit-universe
    parent: manage-deployments
    weight: 60
type: docs
---

YugabyteDB Anywhere allows you to expand a universe to add more nodes and shrink the universe to reduce the number of nodes. Typically, you do this by navigating to **Universes > Edit Universe**, as shown in the following illustration:

![Edit universe](/images/ee/edit-univ.png)

Using the **Edit Universe** page, you can specify the new user intent for the universe. This may include a new configuration of nodes powered by a different instance type.

YugabyteDB Anywhere performs these modifications through the YB-Masters powering the universe. The YB-Masters ensure that the new nodes start hosting the tablet leaders for a set of tablets in such a way that the tablet leader count remains evenly balanced across all the available nodes.

Expansion of universes created with an on-premise cloud provider and secured with third-party certificates obtained from external certification authorities follows a different workflow. For details, see [How to Expand the Universe](../../security/enable-encryption-in-transit#how-to-expand-the-universe) .
