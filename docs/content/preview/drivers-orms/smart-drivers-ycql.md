---
title: YugabyteDB YCQL drivers
linkTitle: Smart drivers
description: Use YugabyteDB drivers to improve performance with partition-aware load balancing and JSON support for YCQL
headcontent: Manage partition-aware load balancing automatically using YCQL drivers
menu:
  preview:
    identifier: smart-drivers-ycql
    parent: drivers-orms
    weight: 400
type: docs
---

{{<tabs>}}
{{<tabitem href="../smart-drivers/" text="YSQL" icon="postgres" >}}
{{<tabitem href="../smart-drivers-ycql/" text="YCQL" icon="cassandra" active="true" >}}
{{</tabs>}}

The standard/official drivers available for Cassandra work out-of-the-box with [YugabyteDB YCQL API](../../api/ycql/).

Yugabyte has extended the upstream Cassandra drivers with specific changes to leverage YugabyteDB features, available as open source software under the Apache 2.0 license.

| GitHub project | Based on | Learn more |
| :--- | :--- | :--- |
| [YugabyteDB Java Driver for YCQL (3.10)](https://github.com/yugabyte/cassandra-java-driver/tree/3.10.0-yb-x) | [DataStax Java Driver 3.10](https://docs.datastax.com/en/developer/java-driver/3.10/) | [Documentation](../java/ycql/) |
| [YugabyteDB Java Driver for YCQL (4.15)](https://github.com/yugabyte/cassandra-java-driver/tree/4.15.x) | [DataStax Java Driver 4.15](https://docs.datastax.com/en/developer/java-driver/4.15/) | [Documentation](../java/ycql-4.x/) |
| [YugabyteDB Go Driver for YCQL](https://github.com/yugabyte/gocql) | [gocql](https://gocql.github.io/) | [Documentation](../go/ycql/) |
| [YugabyteDB Python Driver for YCQL](https://github.com/yugabyte/cassandra-python-driver) | [DataStax Python Driver](https://github.com/datastax/python-driver) | [Documentation](../python/ycql/) |
| [YugabyteDB Node.js Driver for YCQL](https://github.com/yugabyte/cassandra-nodejs-driver) | [DataStax Node.js Driver](https://github.com/datastax/nodejs-driver) | [Documentation](../nodejs/ycql/) |
| [YugabyteDB C++ Driver for YCQL](https://github.com/yugabyte/cassandra-cpp-driver) | [DataStax C++ Driver](https://github.com/datastax/cpp-driver) | [Documentation](../cpp/ycql/) |
| [YugabyteDB C# Driver for YCQL](https://github.com/yugabyte/cassandra-csharp-driver) | [DataStax C# Driver](https://github.com/datastax/csharp-driver) | [Documentation](../csharp/ycql/) |
| [YugabyteDB Ruby Driver for YCQL](https://github.com/yugabyte/cassandra-ruby-driver) | [DataStax Ruby Driver](https://github.com/datastax/ruby-driver) | [Documentation](../rust/yb-rust-postgres) |

## Key features

YugabyteDB YCQL drivers have the following key features.

| Feature | Notes |
| :--- | :--- |
| [Retry policy](#retry-policy) | Like the upstream driver, the YugabyteDB YCQL driver retries certain operations in case of failures. |
| [Multiple contact points](#multiple-contact-points) | As with the upstream driver, you can specify multiple contact points for the initial connection, to avoid dropping connections in the case where the primary contact point is unavailable. |
| [Partition-aware load balancing policy](#partition-aware-load-balance-policy) | (YugabyteDB driver only) You can target a specific tablet or node for a particular query where the required data is hosted. |
| [JSONB support](#jsonb-support) | (YugabyteDB driver only) YugabyteDB YCQL drivers support the use of the JSONB data type for table columns. |

## Using YugabyteDB YCQL drivers

YugabyteDB is a distributed, fault tolerant, and highly available database with low latencies for reads and writes. Data in YugabyteDB is automatically sharded, replicated, and balanced across multiple nodes that can potentially be in different availability zones and regions. For better performance and fault tolerance, you can also balance application traffic (database connections) across the nodes in the universe to avoid excessive CPU and memory load on any single node.

### Retry policy

As with the upstream Cassandra drivers, YugabyteDB YCQL drivers can retry certain operations if they fail the first time. This is governed by a retry-policy. The default number of retries is one for a failed operation for certain cases, depending on the language driver. The retry may happen on the same node or the next available node as per the query's plan.

Some drivers allow you to provide a custom retry policy.
Refer to [Retries](https://docs.datastax.com/en/developer/java-driver/4.15/manual/core/retries/#retries) in DataStax documentation for information on built-in retry polices.

### Multiple contact points

Usually, the contact point is the host address given in the connection configuration by the application. The driver creates a control connection to this host address. If this host address is unavailable at the time of application's initialization, the application itself fails.
To avoid this scenario, you can specify multiple comma-separated host addresses for your applications as contact points. If the first host is not accessible, the driver tries to create a control connection to the next contact point, and so on, until it succeeds or all contact points are attempted.

### YugabyteDB-specific features

Yugabyte has extended the standard Cassandra drivers with the following additional features to further optimize the performance of YugabyteDB clusters.

These features are enabled by default.

#### Partition-aware load balance policy

The YugabyteDB drivers provide a few built-in load balancing policies which govern how requests from the client applications should be routed to a particular node in the cluster. Some policies prefer nodes in the local datacenter, whereas others select a node randomly from the entire cluster irrespective of the datacenter they are in.

YugabyteDB partitions its table data across the nodes in the cluster as tablets, and labels one copy of each tablet as the _leader_ tablet and other copies as _follower_ tablets.
The YCQL drivers have been modified to make use of this distinction, and optimize the read and write performance, by introducing a new load balancing policy.

Whenever it is possible to calculate the hash of the partitioning key (token in Cassandra) from a query, the driver figures out the nodes hosting the leader/follower tablets and sends the query to the appropriate node. The decision on which node gets picked up is also determined by the values of Consistency Level (CL) and local datacenter specified by the application. YugabyteDB supports only two CLs: QUORUM and ONE. If none is specified, it considers QUORUM.

<!-- <<table of how combination of CL and localDC affect node selection (for Java alone?)>> -->

#### JSONB support

YugabyteDB supports JSONB data type, similar to PostgreSQL, and this data type is not supported by the upstream Cassandra drivers.

For information on how to define and handle table columns with JSONB data type, see [JSON Support in YCQL](../../explore/ycql-language/jsonb-ycql/).

## Connection handling

YCQL drivers maintain a pool of connections for each node in a cluster.
When a client application starts, the driver attempts to create a control connection to the node specified as [contact point](#multiple-contact-points). After the connection is established, the driver fetches the information about all the nodes in the cluster, and then creates a pool of connections for each of those nodes.

You can configure the size of the connection pool. For example, in the Java driver, you can define different pool sizes for nodes in a local datacenter, and for those in other (remote) datacenters. If the number of connections in a pool go lower than the defined size for any reason, the driver immediately tries to fill the gap in the background.

## Using YCQL drivers with YugabyteDB Managed

[YugabyteDB Managed](../../yugabyte-cloud/) clusters automatically use load balancing provided by the cloud provider where the cluster is provisioned. The nodes are not directly accessible to the outside world. This is in contrast to the desired setup of YCQL drivers, which need direct access to all the nodes in a cluster.

The drivers still work in this situation, but the client applications lose some of the benefits of the driver, including [partition-aware query routing](#partition-aware-load-balance-policy) and the default [retry policy](#retry-policy).

To take advantage of the driver's partition-aware load balancing feature when connecting to clusters in YugabyteDB Managed, the applications must be deployed in a VPC that has been peered with the cluster VPC; VPC peering enables the client application to access all nodes of the cluster. For information on VPC peering in YugabyteDB Managed, refer to [VPC network](../../yugabyte-cloud/cloud-basics/cloud-vpcs/).

If VPC peering is not possible, you can attempt to restore the retry capability by increasing the size of the connection pool (the Java driver default size is 1) and providing a custom retry policy to retry the failed operation on the same node.

Note that there is no workaround for partition-aware query routing.

## Learn more

- [Build java applications using Apache Spark and YugabyteDB](/preview/integrations/apache-spark/java-ycql/) where YugabyteDB has forked a Cassandra Connector for Apache Spark.
