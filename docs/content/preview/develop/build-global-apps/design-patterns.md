---
title: Design patterns for global applications
headerTitle: Design patterns for global applications
linkTitle: Design patterns
description: Build global applications using stand design patterns
headcontent: Learn how to design globally distributed applications using simple patterns
image: /images/section_icons/quick_start/sample_apps.png
menu:
  preview:
    identifier: global-apps-design-patterns
    parent: build-global-apps
    weight: 202
rightNav:
  hideH3: true
  hideH4: true
type: docs
---

Deploying applications in multiple data centers and splitting data across them can be a complex undertaking. However, YugabyteDB can be deployed in various configurations, such as single-region multi-zone, or multi-region multi-zone. You can leverage some of our battle-tested design paradigms, which offer solutions to common problems faced in these scenarios. By adopting such design patterns, your application development can be significantly accelerated. These proven paradigms offer solutions that can save time and resources that would otherwise be spent reinventing the wheel.

Let's look at a few classes of design patterns that you can adopt with YugabyteDB.

## Fault Tolerance and High Availability

To provide uninterrupted service to your users, it's crucial to ensure that your applications can handle machine failures, network outages, and power failures. Global applications must be deployed in multiple locations with standby locations that can take over when the primary location fails. You can deploy YugabyteDB in the following configurations to ensure your applications are highly available.

### Multi-region cluster with synchronous replication

You can set up your cluster across different regions/zones with multiple replicas (typically 3) such that the replicas are in different regions/zones. When a node fails in a region or an entire region/zone fails, a replica in another region/zone will be promoted to leader in seconds, without any loss of data. This is possible because of the [synchronous replication using the raft consensus protocol](../../../architecture/docdb-replication/replication).

{{<tip>}}
For more information, see  [Stretch Cluster](./design-patterns-ha#stretch-cluster)
{{</tip>}}

### Dual cluster with async replication

You can set up a separate cluster with [xCluster](../../../architecture/docdb-replication/async-replication/) replication. Replication can be configured to be either unidirectional, which is useful to create a standby cluster or bidirectional, which would enable you to write to both clusters at the same time. The key thing to remember in xCluster is that it uses asynchronous replication, which means updates will not wait for the other universe to catch up and the two clusters could be out of sync for a while.

{{<tip>}}
For more information, see  [Active-Active Multi-Master](./design-patterns-ha#active-active-multi-master)
{{</tip>}}

## Performance

### Reduce latency with preferred leaders

By default, all leaders are distributed across multiple regions in a global application. As all writes and reads go to the leaders this leads to an increase in latency even for a simple query as the table leader and index leader could be in different regions. You can ensure all leaders are located in the same region by setting up preferred zones for leaders.

{{<tip>}}
For more details, see  [Preferred Leaders](./global-performance#reducing-latency-with-preferred-leaders)
{{</tip>}}

### Reduce read latency with read replicas

Just like how you can set up alternate clusters and replicas for fault tolerance, you can set up a separate [read replica](../../../architecture/docdb-replication/read-replicas/) cluster to improve the read latency in a different region. Read replicas are non-voting members of the raft group and can have a different replication factor from the primary cluster.

{{<tip>}}
For more information, see  [Read Replicas](./design-patterns-ha#read-replica)
{{</tip>}}

## Compliance

To comply with data residency laws in various countries, you might have to place the data of their respective citizens in data centers in that country. YugabyteDB offers a few patterns to ensure such rules are complied with correctly.

### Pinning tables to local geographies

Different tables can be attached to different [tablespaces](../../../explore/ysql-language-features/going-beyond-sql/tablespaces/) setup for different geographies. With this, you can ensure that certain tables are in specific geographies.

{{<tip>}}
For more details, see  [Geo local tables](./design-patterns-compliance#geo-local-tables)
{{</tip>}}

### Pinning partitions to local geographies

Your table data can be [partitioned](../../../explore/ysql-language-features/advanced-features/partitions/) and attached to [tablespaces](../../../explore/ysql-language-features/going-beyond-sql/tablespaces/) setup for different geographies. With this, you can ensure that the rows belonging to different users will be located in their respective countries.

{{<tip>}}
For more details, see  [Pinning partitions](./design-patterns-compliance#geo-partitioned-tables)
{{</tip>}}


