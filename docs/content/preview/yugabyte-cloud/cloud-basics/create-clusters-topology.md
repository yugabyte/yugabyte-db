---
title: Choose a topology
linkTitle: Choose a topology
description: Overview of topologies available in YugabyteDB Managed.
headcontent: Deployment and replication options in YugabyteDB Managed
menu:
  preview_yugabyte-cloud:
    identifier: create-clusters-topology
    parent: cloud-basics
    weight: 15
type: docs
---

A YugabyteDB cluster consists of three or more nodes that communicate with each other and across which data is distributed. You can place the nodes of a YugabyteDB cluster across different zones in a single region, and across regions. The topology you choose depends on your requirements for latency, availability, and geo-distribution:

- Moving data closer to where end-users are enables lower latency access.
- Distributing data can make the data service resilient to zone and region outages in the cloud.
- Geo-partitioning can keep user data in a particular geographic region to comply with data sovereignty regulations.

YugabyteDB Managed offers a number of deployment and replication options in geo-distributed environments to achieve resilience, performance, and compliance objectives.

| Type | Consistency | Read Latency | Write Latency | Best For |
| :--- | :--- | :--- | :--- | :--- |
| [Multi zone](#single-region-multi-zone-cluster) | Strong | Low in region (1-10ms) | Low in region (1-10ms) | Zone-level resilience |
| [Replicate across regions](#replicate-across-regions) | Strong | High with strong consistency or low with eventual consistency | Depends on inter-region distances | Region-level resilience |
| [Partition by region](#partition-by-region) | Strong | Low in region (1-10ms); high across regions (40-100ms) | Low in region (1-10ms); high across regions (40-100ms) | Compliance, low latency I/O by moving data closer to customers |
| [Read replica](#read-replicas) | Strong in primary, eventual in replica | Low in region (1-10ms) | Low in primary region (1-10ms) | Low latency reads |

For more information on replication and deployment strategies for YugabyteDB, see the following:

- [DocDB replication layer](../../../architecture/docdb-replication/)
- [Multi-region deployments](../../../explore/multi-region-deployments/)
- [Engineering Around the Physics of Latency](https://vimeo.com/548171949)
- [9 Techniques to Build Cloud-Native, Geo-Distributed SQL Apps with Low Latency](https://www.yugabyte.com/blog/9-techniques-to-build-cloud-native-geo-distributed-sql-apps-with-low-latency/)
- [Geo-partitioning of Data in YugabyteDB](https://www.yugabyte.com/blog/geo-partitioning-of-data-in-yugabytedb/)

<!--
| xCluster active-passive | Strong | Low in region (1-10ms) | Low in region (1-10ms) | Backup and data recovery, low latency I/O |
| xCluster active-active | Eventual (timeline) | Low in region (1-10ms) | Low in region (1-10ms) | Backup and data recovery-, low latency I/O |
-->

## Single region multi-zone cluster

In a single-region multi-zone cluster, the nodes of the YugabyteDB cluster are placed across different availability zones in a single region.

![Single cluster deployed across three zones in a region](/images/yb-cloud/Geo-Distribution-Blog-Post-Image-1.png)

**Resilience**: Cloud providers design zones to minimize the risk of correlated failures caused by physical infrastructure outages like power, cooling, or networking. In other words, single failure events usually affect only a single zone. By deploying nodes across zones in a region, you get resilience to a zone failure as well as high availability.

**Consistency**: YugabyteDB automatically shards the tables of the database, places the data across the nodes, and replicates all writes synchronously. The cluster ensures strong consistency of all I/O and distributed transactions.

**Latency**: Because the zones are close to each other, you get low read and write latencies for clients located in the same region as the cluster, typically 1 – 10ms latency for basic row access.

**Strengths**

- Strong consistency
- Resilience and high availability (HA) – zero recovery point objective (RPO) and near zero recovery time objective (RTO)
- Clients in the same region get low read and write latency

**Tradeoffs**

- Applications accessing data from remote regions may experience higher read/write latencies
- Not resilient to region-level outages, such as those caused by natural disasters like floods or ice storms

**Fault tolerance**

Availability Zone Level, with a minimum of 3 nodes across 3 availability zones in a single region.

**Deployment**

To deploy a multi-zone cluster, create a single-region cluster with Availability Zone Level fault tolerance. Refer to [Create a single-region cluster](../create-clusters/create-single-region/).

<!--If you deploy your cluster in a VPC, you can [geo-partition](#partition-by-region) the cluster after it is created.-->

## Replicate across regions

In a cluster that is replicated across regions, the nodes of the cluster are deployed in different regions rather than in different availability zones of the same region.

![Single cluster deployed across three regions](/images/yb-cloud/Geo-Distribution-Blog-Post-Image-2.png)

**Resilience**: Putting cluster nodes in different regions provides a higher degree of failure independence. In the event of a region failure, the database cluster continues to serve data requests from the remaining regions. YugabyteDB automatically performs a failover to the nodes in the other two regions, and the tablets being failed over are evenly distributed across the two remaining regions.

**Consistency**: All writes are synchronously replicated. Transactions are globally consistent.

**Latency**: Latency in a multi-region cluster depends on the distance and network packet transfer times between the nodes of the cluster and between the cluster and the client. Write latencies in this deployment mode can be high. This is because the tablet leader replicates write operations across a majority of tablet peers before sending a response to the client. All writes involve cross-zone communication between tablet peers.

As a mitigation, you can enable [follower reads](../../../explore/going-beyond-sql/follower-reads-ysql/) and set a [preferred region](../create-clusters/create-clusters-multisync/#preferred-region).

**Strengths**

- Resilience and HA – zero RPO and near zero RTO
- Strong consistency of writes, tunable consistency of reads

**Tradeoffs**

- Write latency can be high (depends on the distance/network packet transfer times)
- [Follower reads](../../../explore/going-beyond-sql/follower-reads-ysql) trade off consistency for latency

**Fault tolerance**

Region Level, with a minimum of 3 nodes across 3 regions.

**Deployment**

To deploy a multi-region replicated cluster, refer to [Replicate across regions](../create-clusters/create-clusters-multisync/).

**Learn more**

- [Synchronous multi-region](../../../explore/multi-region-deployments/synchronous-replication-cloud/)
- [Synchronous replication](../../../architecture/docdb-replication/replication/)

## Partition by region

Applications that need to keep user data in a particular geographic region to comply with data sovereignty regulations can use row-level geo-partitioning in YugabyteDB. This feature allows fine-grained control over pinning rows in a user table to specific geographic locations.

Here's how it works:

1. Pick a column of the table that will be used as the partition column. The value of this column could be the country or geographic name in a user table for example.

2. Next, create partition tables based on the partition column of the original table. You will end up with a partition table for each region that you want to pin data to.

3. Finally pin each table so the data lives in different zones of the target region.

With this deployment mode, the cluster automatically keeps specific rows and all the table shards (known as tablets) in the specified region. In addition to complying with data sovereignty requirements, you also get low-latency access to data from users in the region while maintaining transactional consistency semantics.

In YugabyteDB Managed, a partition-by-region cluster consists initially of a primary region where all tables that aren't geo-partitioned (that is, don't reside in a tablespace) reside, and any number of additional regions where you can store partitioned data, whether it's to reduce latencies or comply with data sovereignty requirements. Tablespaces are automatically placed in all the regions.

![Geo-partitioned cluster deployed across three regions](/images/yb-cloud/Geo-Distribution-Blog-Post-Image-5.png)

**Resilience**: Clusters with geo-partitioned tables are resilient to zone-level failures when the nodes in each region are deployed in different zones of the region.

**Consistency**: Because this deployment model has a single cluster that is spread out across multiple geographies, all writes are synchronously replicated to nodes in different zones of the same region, thus maintaining strong consistency.

**Latency**: Because all the tablet replicas are pinned to zones in a single region, read and write overhead is minimal and latency is low. To insert rows or make updates to rows pinned to a particular region, the cluster needs to touch only tablet replicas in the same region.

**Strengths**

- Tables that have data that needs to be pinned to specific geographic regions to meet data sovereignty requirements
- Low latency reads and writes in the region where the data resides
- Strongly consistent reads and writes

**Tradeoffs**

- Row-level geo-partitioning is helpful for specific use cases where the dataset and access to the data is logically partitioned. Examples include users in different countries accessing their accounts, and localized products (or product inventory) in a product catalog.
- When users travel, access to their data will incur cross-region latency because their data is pinned to a different region.

**Fault tolerance**

The regions in the cluster can have fault tolerance of None (single node, no HA), Node Level (minimum 3 nodes in a single availability zone), or Availability Zone Level (minimum 3 nodes in 3 availability zones; recommended). Any regions you add to the cluster have the same fault tolerance as the primary region.

**Deployment**

To deploy a partition-by-region cluster, refer to [Partition by region](../create-clusters/create-clusters-geopartition/).

**Learn more**

- [Row-level geo-partitioning](../../../explore/multi-region-deployments/row-level-geo-partitioning/)
- [Tablespaces](../../../explore/going-beyond-sql/tablespaces/)

<!--
## Cross-universe

WARNING: This section is somewhat out of date, especially with regards to consistency; see [xCluster replication](../../../architecture/docdb-replication/async-replication/) for up-to-date information.  Also, note that YBM  does not use the word "universe" currently.

In situations where applications want to keep data in multiple clouds or in remote regions, YugabyteDB offers xCluster replication across two data centers or cloud regions. This can be either bi-directional in an active-active configuration, or uni-directional in an active-passive configuration.

Here's how it works:

1. You deploy two YugabyteDB universes (typically) in different regions. Each universe automatically replicates data in itself synchronously for strong consistency.

2. You then set up xCluster asynchronous replication from one universe to another. This can be either bi-directional in active-active configurations, or uni-directional in active-passive configurations.

**Deployment**

To deploy a xCluster replication universe, first [deploy your source universe](#single-region-multi-zone-cluster), then contact {{% support-cloud %}} to add the target universe.

**Learn more**

[xCluster replication](../../../architecture/docdb-replication/async-replication/)

### Active-passive

In an active-passive configuration, one universe handles writes, and asynchronously replicates to a target universe.

The target universe can be used to serve low-latency reads that are timeline consistent to clients nearby. They can also be used for disaster recovery. In the event of a source universe failure, clients can connect to the replicated target universe.

This configuration is used for use cases such as data recovery, auditing, and compliance. You can also use xCluster replication to migrate data from a data center to the cloud or from one cloud to another. In situations that tolerate eventual consistency, clients in the same region as the target universes can get low latency reads.

![Multi-region deployment with single-direction xCluster replication between universe](/images/yb-cloud/Geo-Distribution-Blog-Post-Image-3.png)

**Resilience**: If you deploy the nodes of each universe across zones, you get zone-level resilience. In addition, this topology also gives you disaster recovery in the event that the source universe goes down.

**Consistency**: Reads and writes in the source universe are strongly consistent. Because replication across universes is asynchronous, I/O in the target universe will be timeline consistent, with transactions seeing a consistent snapshot sometime in the past.

**Latency**: With xCluster, replication to the remote universe happens outside the critical path of a write operation. So replication doesn't materially impact latency of reads and writes. In essence you are trading off consistency for latency. Reads in the regions with a universe have low latency.

**Strengths**

- Disaster recovery – non-zero RPO and non- zero RTO
- Timeline consistency in the target universe, strong consistency in the source universe
- Low latency reads and writes in the source universe region

**Tradeoffs**

- The target universe doesn't handle writes. Writes from clients outside the source universe region can incur high latency
- Because xCluster replication bypasses the query layer for replicated records, database triggers won't get fired and can lead to unexpected behavior

### Active-active

In an active-active configuration, both universes can handle writes to potentially the same data. Writes to either universe are asynchronously replicated to the other universe with a timestamp for the update. xCluster with bi-directional replication is used for disaster recovery.

![Multi-region deployment with bi-directional xCluster replication between universes](/images/yb-cloud/Geo-Distribution-Blog-Post-Image-4.png)

**Resilience**: If you deploy the nodes of each universe across zones, you get zone-level resilience. In addition, this topology also gives you disaster recovery if either universe goes down.

**Consistency**: Reads are eventually consistent and the last writer wins.  For more details, including inconsistencies affecting transactions, see [non-transactional replication](../../../architecture/docdb-replication/async-replication/#non-transactional-replication).

**Latency**: With xCluster, replication to the remote universe happens outside the critical path of a write operation. So replication doesn't materially impact latency of reads and writes. In essence you are trading off consistency for latency.

**Strengths**

- Disaster recovery – non-zero RPO and non- zero RTO
- Low latency reads and writes in either universe

**Tradeoffs**

- Because of the reduced consistency, applications have to be able to handle eventual consistency.  In the event of an unplanned failover, the inconsistency can become permanent.
- Because xCluster replication bypasses the query layer for replicated records, database triggers won't get fired and can lead to unexpected behavior
- Because xCluster replication is done at the write-ahead log (WAL) level, there is no way to check for unique constraints. It's possible to have two conflicting writes in separate universes that will violate the unique constraint and will cause the main table to contain both rows but the index to contain just 1 row, resulting in an inconsistent state.
- Similarly, the active-active mode doesn't support auto-increment IDs because both universes will generate the same sequence numbers, and this can result in conflicting rows. It is better to use UUIDs instead.
-->

## Read replicas

For applications that have writes happening from a single zone or region but want to serve read requests from multiple remote regions, you can use read replicas. Data from the primary cluster is automatically replicated asynchronously to one or more read replica clusters. The primary cluster gets all write requests, while read requests can go either to the primary cluster or to the read replica clusters depending on which is closest. To read data from a read replica, you enable [follower reads](../../../explore/going-beyond-sql/follower-reads-ysql/) for the cluster.

![Read replicas](/images/yb-cloud/Geo-Distribution-Blog-Post-Image-6.png)

**Resilience**: If you deploy the nodes of the primary cluster across zones or regions, you get zone- or region-level resilience. Read replicas don't participate in the Raft consistency protocol and therefore don't affect resilience.

**Consistency**: The data in the replica clusters is timeline consistent, which is better than eventual consistency.

**Latency**: Reads from both the primary cluster and read replicas can be fast (single digit millisecond latency) because read replicas can serve timeline consistent reads without having to go to the [tablet leader](../../../architecture/key-concepts/#tablet-leader) in the primary cluster. Read replicas don't handle write requests; these are redirected to the primary cluster. So the write latency will depend on the distance between the client and the primary cluster.

**Strengths**

- Fast, timeline-consistent reads from replicas
- Strongly consistent reads and writes to the primary cluster
- Low latency writes in the primary region

**Tradeoffs**

- The primary cluster and the read replicas are correlated clusters, not two independent clusters. In other words, adding read replicas doesn't improve resilience.
- Read replicas can't take writes, so write latency from remote regions can be high even if there is a read replica near the client.

**Fault tolerance**

Read replicas have a minimum of 1 node. Adding nodes to the read replica increases the replication factor (that is, adds copies of the data) to protect the read replica from node failure.

**Deployment**

You can add replicas to an existing primary cluster as needed. Refer to [Read replicas](../../cloud-clusters/managed-read-replica/).

**Learn more**

- [Read replicas](../../../architecture/docdb-replication/read-replicas/)
- [Follower reads](../../../explore/going-beyond-sql/follower-reads-ysql/)
