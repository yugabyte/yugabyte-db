---
title: Best Practices
headerTitle: Best Practices
linkTitle: Best Practices
description: Best Practices for Change Data Capture in YugabyteDB.
menu:
  preview:
    parent: explore-change-data-capture-logical-replication
    identifier: best-practices-cdc
    weight: 60
type: docs
---

This section mentions best practices to achieve scalability and performance while using CDC with logical replication.

## Parallel consumption

The recommended approach towards addressing the requirement of consuming changes in parallel from different tables is to use multiple replication slots. One replication slot per table could be used. Each replication slot is independent of the other and the changes from the tables can be consumed in parallel.

## Fan out 

Consider the requirement where there are multiple applications, all of them requiring to consume changes from the same table. The recommended approach to address this requirement is to use one replication slot to consume the changes from the table and write the changes to a system like Kafka. The fan out can then be implemented with the multiple applications consuming from Kafka.

## Load balancing consumption

An application can connect to any of the tserver nodes to consume from a replication slot. Furthermore, even in case of an interruption, a fresh connection can be made to a different node (different from the node from which consumption was previously happening) to continue consumption from the same replication slot.

When there are multiple consuming applications each consuming from a different replication slot, it is best that the applications connect to different tserver nodes in the cluster. This ensures better load balancing. The [YugabyteDB smart driver](../../../drivers) does this automatically, so it is recommended that applications use this smart driver.
