---
title: Best Practices for logical replication
headerTitle: Best practices
linkTitle: Best practices
description: Best Practices for logical replication with Change Data Capture in YugabyteDB.
menu:
  v2025.1:
    parent: explore-change-data-capture-logical-replication
    identifier: best-practices-cdc
    weight: 60
type: docs
---

This section describes best practices to achieve scalability and performance while using CDC with logical replication.

## Parallel consumption

The recommended approach towards addressing the requirement of consuming changes in parallel from different tables is to use multiple replication slots. One replication slot per table could be used. Each replication slot is independent of the other and the changes from the tables can be consumed in parallel.

## Fan out

Consider the requirement where there are multiple applications, all of them requiring to consume changes from the same table. The recommended approach to address this requirement is to use one replication slot to consume the changes from the table and write the changes to a system like Kafka. The fan out can then be implemented with the multiple applications consuming from Kafka.

## Load balancing consumption

An application can connect to any of the YB-TServer nodes to consume from a replication slot. Furthermore, even in case of an interruption, a fresh connection can be made to a different node (different from the node from which consumption was previously happening) to continue consumption from the same replication slot.

When there are multiple consuming applications each consuming from a different replication slot, it is best that the applications connect to different YB-TServer nodes in the cluster. This ensures better load balancing. The [YugabyteDB smart driver](/stable/develop/drivers-orms/smart-drivers/) does this automatically, so it is recommended that applications use this smart driver.

## Avoid reusing Kafka topics across slots

When you use Kafka Connect to consume YugabyteDB logical replication slots and stream changes to Apache Kafka, Kafka Connect persists last received LSN for each replication slot. Deleting a connector does not clear these offsets.

If you drop a replication slot is dropped and recreate it with the same slot name, and then deploy a connector to stream change events using this replication slot to the previously created Kafka topics, the connector would try to start streaming from the LSN stored as per the previous replication slot.

In YugabyteDB CDC, with `SEQUENCE` LSN type, the LSNs are not comparable across slots. So, such a deployment can cause the new slot to miss sending some records. This causes data loss in your Kafka topics: downstream systems see incomplete change history. To prevent this from happening, always ensure each replication slot streams to its own set of Kafka topics by using a different [topic.prefix](../yugabytedb-connector-properties/#topic-prefix) for every connector which polls using a new replication slot.
