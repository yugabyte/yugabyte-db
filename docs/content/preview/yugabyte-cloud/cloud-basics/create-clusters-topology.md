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
- Distributing data can make the data service resilient to zone and region failures in the cloud.
- Geo-partitioning can keep user data in a particular geographic region to comply with data sovereignty regulations.

YugabyteDB Managed offers a number of deployment and replication options in geo-distributed environments to achieve resilience, performance, and compliance objectives.

| Type | Consistency | Read Latency | Write Latency | Best For |
| :--- | :--- | :--- | :--- | :--- |
| Multi zone | Strong | Low in region (1-10ms) | Low in region (1-10ms) | Zone-level resilience |
| Replicate across regions | Strong | High with strong consistency or low with eventual consistency | Depends on inter-region distances | Region-level resilience |
| Partition by region | Strong | Low in region (1-10ms); high across regions (40-100ms) | Low in region (1-10ms); high across regions (40-100ms) | Compliance, low latency I/O by moving data closer to customers |
| xCluster active-passive | Strong | Low in region (1-10ms) | Low in region (1-10ms) | Backup and data recovery, low latency I/O |
| xCluster active-active | Eventual (timeline) | Low in region (1-10ms) | Low in region (1-10ms) | Backup and data recovery-, low latency I/O |
| Read replica | Strong in source, eventual in replica | Low in primary region (1-10ms) | Low in region (1-10ms) | Low latency reads |

## Single region multi-zone cluster

In a single-region multi-zone cluster, the nodes of the YugabyteDB cluster are placed across different availability zones in a single region.

![Single cluster deployed across three zones in a region](/images/yb-cloud/Geo-Distribution-Blog-Post-Image-1.png)

**Resilience**: Cloud providers like AWS and Google Cloud design zones to minimize the risk of correlated failures caused by physical infrastructure outages like power, cooling, or networking. In other words, single failure events usually affect only a single zone. By deploying nodes across zones in a region, you get resilience to a zone failure as well as high availability.

**Consistency**: YugabyteDB automatically shards the tables of the database, places the data across the nodes, and replicates all writes synchronously. The cluster ensures strong consistency of all I/O and distributed transactions.

**Latency**: Because the zones are close to each other, you get low read and write latencies for clients located in the same region as the cluster, typically 1 – 10ms latency for basic row access.

**Strengths**

- Strong consistency
- Resilience and high availability (HA) – zero recovery point objective (RPO) and near zero recovery time objective (RTO)
- Clients in the same region get low read and write latency

**Tradeoffs**

- Applications accessing data from remote regions may experience higher read/write latencies
- Not resilient to region-level outages, such as those caused by natural disasters like floods or ice storms

**Deployment**

To deploy a multi-zone cluster, create a single-region cluster with Availability Zone Level fault tolerance. Refer to [Create a single-region cluster](../create-clusters/create-single-region/).

If you deploy your cluster in a VPC, you can [geo-partition](#partition-by-region) the cluster after it is created.

## Replicate across regions

In a cluster that is replicated across regions, the nodes of the cluster are deployed in different regions rather than in different availability zones of the same region.

![Single cluster deployed across three regions](/images/yb-cloud/Geo-Distribution-Blog-Post-Image-2.png)

**Resilience**: Putting cluster nodes in different regions provides a  higher degree of failure independence. In the event of a failure, the database cluster continues to serve data requests from the remaining regions while automatically replicating the data in the background to maintain the desired level of resilience.

**Consistency**: All writes are synchronously replicated. Transactions are globally consistent.

**Latency**: Latency in a multi-region cluster depends on the distance/network packet transfer times between the nodes of the cluster and between the cluster and the client. As a mitigation, YugabyteDB offers tunable global reads that allow read requests to trade off some consistency for lower read latency. By default, read requests in a YugabyteDB cluster are handled by the leader of the Raft group associated with the target tablet by default to ensure strong consistency. In situations where you are willing to sacrifice some consistency in favor of lower latency, you can choose to read from a tablet follower that is closer to the client rather than from the leader. YugabyteDB also allows you to specify the maximum staleness of data when reading from tablet followers.

Write latencies in this deployment mode can be high. This is because the tablet leader replicates write operations across a majority of tablet peers before sending a response to the client. All writes involve cross-zone communication between tablet peers.

**Strengths**

- Resilience and HA – zero RPO and near zero RTO
- Strong consistency of writes, tunable consistency of reads

**Tradeoffs**

- Write latency can be high (depends on the distance/network packet transfer times)
- Follower reads trade off consistency for latency

To deploy a multi-region replicated cluster, refer to [Replicate across regions](../create-clusters/create-clusters-multisync/).

## Partition by region

Applications that need to keep user data in a particular geographic region to comply with data sovereignty regulations can use row-level geo-partitioning in YugabyteDB. This feature allows fine-grained control over pinning rows in a user table to specific geographic locations.

Here's how it works:

1. Pick a column of the table that will be used as the partition column. The value of this column could be the country or geographic name in a user table for example.

2. Next, create partition tables based on the partition column of the original table. You will end up with a partition table for each region that you want to pin data to.

3. Finally pin each table so the data lives in different zones of the target region.

With this deployment mode, the cluster automatically keeps specific rows and all the table shards (known as tablets) in the specified region. In addition to complying with data sovereignty requirements, you also get low-latency access to data from users in the region while maintaining transactional consistency semantics.

![Geo-partitioned cluster deployed across three regions](/images/yb-cloud/Geo-Distribution-Blog-Post-Image-5.png)

**Resilience**: Clusters with geo-partitioned tables are resilient to zone-level failures when the nodes in each region are deployed in different zones of the region.

**Consistency**: Because this deployment model has a single cluster that is spread out across multiple geographies, all writes are synchronously replicated to nodes in different zones of the same region, thus maintaining strong consistency.

**Latency**: Because all the shard replicas are pinned to zones in a single region, read and write overhead is minimal and latency is low. To insert rows or make updates to rows pinned to a particular region, the cluster needs to touch only shard replicas in the same region.

**Strengths**

- Tables that have data that needs to be pinned to specific geographic regions to meet data sovereignty requirements
- Low latency reads and writes in the region the data resides in
- Strongly consistent reads and writes

**Tradeoffs**

- Row-level geo-partitioning is helpful for specific use cases where the dataset and access to the data is logically partitioned. Examples include users in different countries accessing their accounts, and localized products (or product inventory) in a product catalog.
- When users travel, access to their data will incur cross-region latency because their data is pinned to a different region.

To deploy a geo-partioned cluster, contact {{% support-cloud %}}.

## Cross-cluster

In situations where applications want to keep data in multiple clouds or in remote regions, YugabyteDB offers xCluster replication across two data centers or cloud regions. This can be either bi-directional in an active-active configuration, or uni-directional in an active-passive configuration.

Here's how it works:

1. You deploy two YugabyteDB clusters (typically) in different regions. Each cluster automatically replicates data in the cluster synchronously for strong consistency.

2. You then set up cross cluster asynchronous replication from one cluster to another. This can be either bi-directional in active-active configurations, or uni-directional in active-passive configurations.

To deploy a cross-cluster replication cluster, contact {{% support-cloud %}}.

### Active-passive

In an active-passive configuration, one cluster handles writes, and asynchronously replicates to a sink cluster.

The sink cluster can be used to serve low-latency reads that are timeline consistent to clients nearby. They can also be used for disaster recovery. In the event of a source cluster failure, clients can connect to the replicated sink cluster.

This configuration is used for use cases such as data recovery, auditing, and compliance. You can also use cross cluster replication to migrate data from a data center to the cloud or from one cloud to another. In situations that tolerate eventual consistency, clients in the same region as the sink clusters can get low latency reads.

![Multi-region deployment with single-direction xCluster replication between clusters](/images/yb-cloud/Geo-Distribution-Blog-Post-Image-3.png)

**Resilience**: If you deploy the nodes of each cluster across zones, you get zone-level resilience. In addition, this topology also gives you disaster recovery in the event that the source cluster goes down.

**Consistency**: Reads and writes in the source cluster are strongly consistent. Because replication across clusters is asynchronous, I/O will be timeline consistent.

**Latency**: With xCluster, replication to the remote cluster happens outside the critical path of a write operation. So replication doesn't materially impact latency of reads and writes. In essence you are trading off consistency for latency. Reads in the regions with a cluster have low latency.

**Strengths**

- Disaster recovery – non-zero RPO and non- zero RTO
- Timeline consistency in the sink cluster, strong consistency in the source cluster
- Low latency reads and writes in the source cluster region

**Tradeoffs**

- The sink cluster doesn't handle writes. Writes from clients outside the source cluster region can incur high latency
- Because xCluster replication bypasses the query layer for replicated records, database triggers won't get fired and can lead to unexpected behavior

### Active-active

In an active-active configuration, both clusters can handle writes to potentially the same data. Writes to either cluster are asynchronously replicated to the other cluster with a timestamp for the update. Cross cluster with bi-directional replication is used for disaster recovery.

![Multi-region deployment with bi-directional xCluster replication between clusters](/images/yb-cloud/Geo-Distribution-Blog-Post-Image-4.png)

**Resilience**: If you deploy the nodes of each cluster across zones, you get zone-level resilience. In addition, this topology also gives you disaster recovery if either cluster goes down.

**Consistency**: Reads and writes in the cluster that handles a write request are strongly consistent. Because replication across clusters is asynchronous, data replication to the remote cluster will be timeline consistent. If the same key is updated in both clusters at a similar time window, this will result in the write with the higher timestamp becoming the latest write (last writer wins semantics).

**Latency**: With xCluster, replication to the remote cluster happens outside the critical path of a write operation. So replication doesn't materially impact latency of reads and writes. In essence you are trading off consistency for latency.

**Strengths**

- Disaster recovery – non-zero RPO and non- zero RTO
- Strong consistency in the cluster that handles a write request, eventual (timeline) consistency in the remote cluster
- Low latency reads and writes in either cluster

**Tradeoffs**

- Because xCluster replication bypasses the query layer for replicated records, database triggers won't get fired and can lead to unexpected behavior
- Because xCluster replication is done at the write-ahead log (WAL) level, there is no way to check for unique constraints. It's possible to have two conflicting writes in separate universes that will violate the unique constraint and will cause the main table to contain both rows but the index to contain just 1 row, resulting in an inconsistent state.
- Similarly, the active-active mode doesn't support auto-increment IDs because both universes will generate the same sequence numbers, and this can result in conflicting rows. It is better to use UUIDs instead.

## Read replicas

For applications that have writes happening from a single zone or region but want to serve read requests from multiple remote regions, you can use read replicas. Data from the primary cluster is automatically replicated asynchronously to one or more read replica clusters. The primary cluster gets all write requests, while read requests can go either to the primary cluster or to the read replica clusters depending on which is closest.

![Read replicas](/images/yb-cloud/Geo-Distribution-Blog-Post-Image-6.png)

**Resilience**: If you deploy the nodes of the primary cluster across zones, you get zone-level resilience. Read replicas don't participate in the Raft consistency protocol and therefore don't affect resilience.

**Consistency**: The data in the replica clusters is timeline consistent, which is better than eventual consistency.

**Latency**: Reads from both the primary cluster and read replicas can be fast (single digit millisecond latency) because read replicas can serve timeline consistent reads without having to go to the shard leader in the primary cluster. Read replicas don't handle write requests; these are redirected to the primary cluster. So the write latency will depend on the distance between the client and the primary cluster.

**Strengths**

- Fast, timeline-consistent reads from replicas
- Strongly consistent reads and writes to the primary cluster
- Low latency writes in the primary region

**Tradeoffs**

- The primary cluster and the read replicas are correlated clusters, not two independent clusters. In other words, adding read replicas doesn't improve resilience.
- Read replicas can't take writes, so write latency from remote regions can be high even if there is a read replica near the client.

To deploy a read replica cluster, contact {{% support-cloud %}}.

## Learn more

- [Multi-DC deployments](../../../deploy/multi-dc/)
- Webinar: [Engineering Around the Physics of Latency](https://vimeo.com/548171949)
- Blog: [9 Techniques to Build Cloud-Native, Geo-Distributed SQL Apps with Low Latency](https://blog.yugabyte.com/9-techniques-to-build-cloud-native-geo-distributed-sql-apps-with-low-latency/)
- Blog: [Geo-partitioning of Data in YugabyteDB](https://blog.yugabyte.com/geo-partitioning-of-data-in-yugabytedb/)

## Next steps

- [Plan your cluster](../create-clusters-overview/)
- [Create a single region cluster](../create-clusters/create-single-region/)
- [Create a synchronous multi-region cluster](../create-clusters/create-clusters-multisync/)
