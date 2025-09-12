---
title: Going beyond PostgreSQL
linkTitle: Beyond PostgreSQL
description: Exclusive advanced features of YugabyteDB
headcontent: Explore exclusive advanced features in YugabyteDB
menu:
  stable:
    identifier: going-beyond-sql
    parent: explore
    weight: 150
type: indexpage
---

While embracing PostgreSQL is a fundamental aspect of YugabyteDB, it extends PostgreSQL by offering a suite of advanced functionalities that address challenges in today's distributed applications like native asynchronous replication,  lowered read latency by reading from closer replicas, extending tablespaces to place data in a specific geography, configurable data distribution, built-in connection pooling, and so on. Let's explore some of these features in detail.

## Read from followers

Use follower reads to spread the read workload across all replicas in the primary cluster. For applications that don't require the latest data or are working with unchanging data, follower reads provide low-latency reads from the primary cluster. However, because data changes are still replicated from the leader, there is a chance of stale reads.

{{<lead link="./follower-reads-ysql/">}}
Learn how [follower reads](./follower-reads-ysql/) work.
{{</lead>}}

## Geo distribution

YugabyteDB allows you to distribute data across different geographic locations based on your specific requirements. This feature is particularly valuable when operating in multiple regions, enabling you to maintain data sovereignty and reduce latency for globally distributed applications. YugabyteDB's tablespace feature integrates seamlessly with its distributed SQL architecture, allowing you to query and manipulate data across regions using standard SQL statements, without worrying about the underlying complexities of data distribution and replication.

{{<lead link="./tablespaces/">}}
Learn how to geo-distribute your data using [tablespaces](./tablespaces).
{{</lead>}}

## Configurable data sharding

Sharding is a fundamental concept that determines how data is partitioned and distributed among multiple nodes, enabling horizontal scalability and high throughput. However, not all workloads and data access patterns are created equal, and a one-size-fits-all approach to data sharding may not always be optimal. YugabyteDB recognizes this and offers two data sharding techniques, Hash and Range, allowing you to tailor data distribution to your specific application requirements.

{{<lead link="./data-sharding/">}}
Learn how and when to choose [hash and range sharding](./data-sharding/).
{{</lead>}}

## Native asynchronous replication

Alongside strongly consistent synchronous replication, YugabyteDB with its xCluster feature, offers asynchronous replication that is designed to replicate data across independent primary clusters, providing a disaster recovery solution that is essential for maintaining business continuity. xCluster can be configured either to be uni-directional or bi-directional.

{{<lead link="./asynchronous-replication-ysql/">}}
Learn how to set up [asynchronous replication](./asynchronous-replication-ysql/).
{{</lead>}}

## Cluster topology

Cluster topology in YugabyteDB involves the arrangement of nodes in a cluster, both in terms of their physical placement and logical organization. Selecting an appropriate cluster topology is important for efficiently designing, deploying, and managing your database infrastructure. You can use the `yb_servers()` function to access the list of nodes in your cluster and their respective locations.

{{<lead link="./cluster-topology/">}}
Learn how to use `yb_servers()` to access your [cluster topology](./cluster-topology/).
{{</lead>}}

## Cluster-aware drivers

YugabyteDB smart drivers are designed to be cluster-aware. This means they know about the cluster's configuration, including which nodes are part of the cluster and their health status immediately after they connect to any single node in the cluster. This allows the drivers to distribute the workload evenly across the cluster, avoiding overloading any single node and ensuring high availability.

{{<lead link="./cluster-aware-drivers/">}}
Learn about the benefits of [cluster-aware drivers](./cluster-aware-drivers/).
{{</lead>}}

## Topology-aware drivers

In addition to being cluster aware, YugabyteDB smart drivers are also aware of the topology. The drivers are aware of the location (that is, the regions and zones) of the nodes in the cluster, and use this information to connect only to specific nodes and failover to a specific set of nodes in case of disasters.

{{<lead link="./topology-aware-drivers/">}}
Learn about the benefits of [topology awareness](./topology-aware-drivers/).
{{</lead>}}

## Built-in connection pooling

YugabyteDB includes a built-in connection pooling manager for YSQL. The manager is designed to overcome the limitations of traditional connection handling methods and make application-side connection pooling unnecessary. It is a server-side connection pooler that allows for the multiplexing of multiple client connections to a smaller number of actual server connections. This not only supports a higher number of concurrent connections but also significantly reduces the overhead associated with creating and managing connections.

{{<lead link="./connection-mgr-ysql/">}}
Learn how to use built-in [connection pooling](./connection-mgr-ysql/).
{{</lead>}}

## Gen-AI applications

As Generative AI (Gen-AI) technologies evolve, integrating them with databases opens new avenues for data-driven decision-making and user interactions. By leveraging chatbots as natural language interfaces, you can effortlessly access and interact with data stored in databases, enhancing accessibility and usability. With YugabyteDB's PostgreSQL compatibility, seamless integration with Gen-AI technologies further streamlines user interactions and data-driven workflows.

{{<lead link="./gen-ai-apps/">}}
Learn how to build [Gen-AI applications for YugabyteDB](./gen-ai-apps/).
{{</lead>}}

## Decouple storage and compute resources

YugabyteDB's flexible architecture allows you to decouple storage and compute resources for improved scalability, independent scaling, and enhanced fault tolerance, and helps you future-proof your systems.

{{<lead link="./decoupling-compute-storage/">}}
Learn how to [decouple storage and compute resources](./decoupling-compute-storage/).
{{</lead>}}
