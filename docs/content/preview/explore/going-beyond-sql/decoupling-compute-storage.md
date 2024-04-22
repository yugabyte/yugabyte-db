---
title: Separating storage and compute
linkTitle: Decouple storage and compute
headcontent: Scale compute and storage layers independently
menu:
  preview:
    identifier: decouple-storage-compute
    parent: going-beyond-sql
    weight: 800
rightNav:
  hideH3: true
type: docs
---

As data volumes and performance demands grow, architectures need to become more adaptable and scalable. Decoupling storage and compute resources can provide improved scalability, independent scaling, and enhanced fault tolerance, and help you future-proof your systems. This article explores the benefits of decoupling storage and compute, and how doing so can transform the way you design, deploy, and manage your distributed database infrastructure.

## Why decouple?

Traditionally, keeping storage close to compute in databases was needed to achieve low latency. But with ultra-fast networks, CPUs, and memory in the cloud era, separating compute and storage resources in databases has advantages:

- **Scalability & Efficiency**. Decoupling allows you to scale each component independently and optimize resource use. You can add or remove computational power and storage capacity as needed. By isolating compute and storage resources, you can optimize the use of each component based on specific workloads. For instance, for growing storage-intensive workloads, it is sufficient to scale just the storage layer, while compute-intensive workloads like query parsing, sorting, aggregation, and so on, can be handled by scaling the compute layer.

- **Heterogeneous Hardware**. Decoupling provides the flexibility to use different hardware configurations for the two layers. For example, you can choose faster CPUs for the computational layer, and select faster solid-state drives (SSDs) for the storage layer.

- **Data placement flexibility**. Decoupling enables you to keep your data layer in specific geographies to comply with data placement laws. It also allows you to specify different security restrictions for the storage and compute layer by placing them in different subnets.

Let us see how to accomplish this separation in YugabyteDB.

## Cluster setup

Set up a local cluster with 6 nodes with IP addresses `127.0.0.[1-6]` and place them in zones `a-f`.

{{<setup/local numnodes=6 locations="aws.east.zone-a,aws.east.zone-b,aws.east.zone-c,aws.east.zone-d,aws.east.zone-e,aws.east.zone-f," >}}

{{<note>}}For clarity, we are placing the nodes across 6 zones, a through f. This also ensures both your compute and storage nodes are spread across multiple zones.{{</note>}}

## Standard behaviour

Typically, the application connects to all the nodes in the cluster. In our local cluster, the application will connect to the 6 nodes as shown in the following illustration.

![Basic cluster](/images/explore/decoupling-compute-storage-setup.png)

Each node has a YB-TServer service that is comprised of the [Query Layer (YQL)](../../../architecture/query-layer) and the [DocDB](../../../architecture/docdb)-based storage layer. Each node in the cluster does both query processing and storage of data. Now let's see how to divide the responsibilities of the compute-heavy query layer and the storage-heavy DocDB layer between the nodes.

## Separating storage

You can use the [Tablespace](../tablespaces) feature in YugabyteDB to restrict the storage to certain zones. Suppose you want to store data only in zones a, b, and c. For this, you need to create a tablespace limited to these zones. For example:

```sql
CREATE TABLESPACE storage
  WITH (replica_placement='{"num_replicas": 3, "placement_blocks": [
    {"cloud":"aws","region":"east","zone":"zone-a","min_num_replicas":1},
    {"cloud":"aws","region":"east","zone":"zone-b","min_num_replicas":1},
    {"cloud":"aws","region":"east","zone":"zone-c","min_num_replicas":1}
]}');
```

Now when you create tables, you have to attach them to these tablespaces. Say you have a user table; when you create the table, attach it to the storage tablespace as follows:

```sql
CREATE TABLE user (
    id INTEGER, name text
) TABLESPACE storage;
```

This automatically ensures that the data is only stored in zones a, b, and c. Now all the nodes located in these zones will be responsible for the storage of data. Nodes in zones d, e, and f will not store any data.

## Separating compute

Now that you have restricted storage to specific zones, the machines in the remaining zones can be used for query processing by having your applications connect only to the nodes in zones d, e, and f. You can either configure your applications to only connect to the remaining nodes (127.0.0.4, 127.0.0.5, 127.0.0.6), or use a [YugabyteDB Smart Driver](../../../drivers-orms/smart-drivers) to connect only to the nodes in zones d, e, and f using [topology_keys](../../../drivers-orms/smart-drivers/#topology-keys). For example:

```java
topology_keys = "topology_keys=aws.east.zone-d,aws.east.zone-e,aws.east.zone-f";
conn_str = "jdbc:yugabytedb://localhost:5433/yugabyte?load-balance=true&" + topology_keys;
```

## Decoupled cluster

You have now effectively divided your cluster into two groups of nodes with distinct responsibilities. Nodes 4-6 process queries and perform sorts and joins, which are compute-heavy. Nodes 1-3 manage the storage of table data. Your setup should look like the following illustration.

![Decoupled cluster](/images/explore/decoupling-compute-storage-final.png)

By separating compute and storage resources, you can build more scalable, resilient, and efficient systems on top of YugabyteDB. These systems can also better adapt to changing data and workload requirements over time.
