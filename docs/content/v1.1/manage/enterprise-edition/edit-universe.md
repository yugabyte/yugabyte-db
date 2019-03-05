---
title: Edit Universe
linkTitle: Edit Universe
description: Edit Universe
menu:
  v1.1:
    identifier: manage-edit-universe
    parent: manage-enterprise-edition
    weight: 730
isTocNested: true
showAsideToc: true
---

Expanding the universe to add more nodes as well as shrinking the universe to lesser number of nodes is as simple as clicking Edit on the Universe page and then providing the new user intent for the universe. The new user intent can even be for an entirely new configuration of nodes powered by a different instance type. YugaWare Admin Console will orchestrate this change via the YB-Masters powering this universe. These YB-Masters ensure that the new nodes start hosting the tablet leaders for a set of tablets in such a way that the tablet leader count remains evenly balanced across all the available nodes. This background data replication is undertaken in anthrottled manner so that the foreground applications are never impacted.

![Edit Universe](/images/ee/edit-univ.png)
