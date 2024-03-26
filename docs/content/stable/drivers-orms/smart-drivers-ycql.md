---
title: YugabyteDB smart drivers for YCQL
linkTitle: Smart drivers
description: Use YugabyteDB smart drivers to improve performance with partition-aware load balancing and JSON support for YCQL
headcontent: Manage partition-aware load balancing automatically using smart drivers
menu:
  stable:
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

In addition to the compatible Cassandra drivers, YugabyteDB also developed the following YCQL drivers with specific changes to leverage YugabyteDB features, available as open source software under the Apache 2.0 license.

| GitHub project | Based on | Learn more |
| :--- | :--- | :--- |
| [YugabyteDB Java Driver for YCQL (3.10)](https://github.com/yugabyte/cassandra-java-driver) | [DataStax Java Driver 3.10](https://docs.datastax.com/en/developer/java-driver/3.10/) | [Documentation](../java/ycql/) |
| [YugabyteDB Java Driver for YCQL (4.15)](https://github.com/yugabyte/cassandra-java-driver/tree/4.15.x) | [DataStax Java Driver 4.15](https://docs.datastax.com/en/developer/java-driver/4.15/) | [Documentation](../java/ycql-4.x/) |
| [YugabyteDB Go Driver for YCQL](https://github.com/yugabyte/gocql) | [gocql](https://gocql.github.io/) | [Documentation](../go/ycql/) |
| [YugabyteDB Python Driver for YCQL](https://github.com/yugabyte/cassandra-python-driver) | [Datastax Python Driver](https://github.com/datastax/python-driver) | [Documentation](../python/ycql/) |
| [YugabyteDB Node.js Driver for YCQL](https://github.com/yugabyte/cassandra-nodejs-driver) | [Datastax Node.js Driver](https://github.com/datastax/nodejs-driver) | [Documentation](../nodejs/ycql/) |
| [YugabyteDB C++ Driver for YCQL](https://github.com/yugabyte/cassandra-cpp-driver) | [Datastax C++ Driver](https://github.com/datastax/cpp-driver) | [Documentation](../cpp/ycql/) |
| [YugabyteDB C# Driver for YCQL](https://github.com/yugabyte/cassandra-csharp-driver) | [Datastax C# Driver](https://github.com/datastax/csharp-driver) | [Documentation](../csharp/ycql/) |
| [YugabyteDB Ruby Driver for YCQL](https://github.com/yugabyte/cassandra-ruby-driver) | [Datastax Ruby Driver](https://github.com/datastax/ruby-driver) | [Documentation](../rust/yb-rust-postgres) |

All YugabyteDB smart driver libraries are actively maintained, and receive bug fixes, performance enhancements, and security patches.

## Key features

YugabyteDB smart drivers have the following key features.

| Feature | Notes |
| :--- | :--- |
| Retry policy | The driver retries certain operations in case of failures. |
| Multiple contact points | Smart driver allows you to provide multiple contact points to ensure that the application initializes successfully even if one or few of the contact points are inaccessible. |
| Partition-aware load balancing policy (YugabyteDB-specific) | Smart driver can target a specific tablet/node for a particular query where the required data is hosted. |
| JSONB support (YugabyteDB-specific) | Smart driver supports the use of JSONB data type for the table columns. |

## Overview

YugabyteDB is a distributed, fault tolerant, and highly available database with low latencies for reads and writes. Data in YugabyteDB is automatically sharded, replicated, and balanced across multiple nodes that can potentially be in different availability zones and regions. For better performance and fault tolerance, you can also balance application traffic (database connections) across the nodes in the universe to avoid excessive CPU and memory load on any single node.

### Retry policy

The smart driver can retry certain operations if they fail the first time, and this is governed by a retry-policy (the default number of retries being one for a failed operation for certain cases). The retry may happen on the same node or the next available node as per the query's plan.

<!-- Some of the drivers allow you to provide a custom retry policy. See the respective language driver’s documentation for more information. -->

### Multiple contact points

Usually, the contact point is the host address given in the connection configuration by the application. The driver creates a control connection to this host address. If this host address is unavailable at the time of application's initialization, the application itself fails.
To avoid such a scenario, you can specify specify comma-separated multiple host addresses for your applications as contact points. If the first host is not accessible, the driver tries to create a control connection to the next contact point, and so on, until it succeeds or all contact points are attempted.

### YugabyteDB-specific additions

Yugabyte has forked some of the standard Cassandra drivers for better performance for YugabyteDB.
Note that no configuration needs to be explicitly specified to _enable_ these YugabyteDB-specific features.

#### Partition-aware load balance policy

The smart driver provides a few built-in load balancing policies which govern how requests from the client applications should be routed to a particular node in the cluster. Some policies prefer nodes in the local datacenter, whereas others select a node randomly from the entire cluster irrespective of the datacenter they are in.

YugabyteDB partitions its table data across the nodes in the cluster as tablets, and labels one copy of each tablet as the _leader_ tablet and other copies as _follower_ tablets.
The smart drivers have been modified to make use of this distinction, and optimize the read and write performance, by introducing a new load balancing policy.

Whenever it is possible to calculate the hash of the partitioning key (token in Cassandra) from a query, the driver figures out the nodes hosting the leader/follower tablets and sends the query to the appropriate node. The decision on which node gets picked up is also determined by the values of Consistency Level (CL) and local datacenter specified by the application. YugabyteDB supports only two Consistency Levels (CL): QUORUM and ONE. If none is specified, it considers QUORUM.

<!-- <<table of how combination of CL and localDC affect node selection (for Java alone?)>> -->

#### JSONB support

YugabyteDB supports JSONB data type, similar to PostgreSQL. This data type is not supported by the upstream/vanilla Cassandra drivers.
Visit this page for more information on how to define and handle table columns with JSONB data type.

## Connection Handling

The driver maintains a pool of connections for each of the nodes in a cluster.
When an application starts, the driver attempts to create a control connection to the node specified as contact point.
Once the connection is established, the driver fetches the information about all the nodes in the cluster. It then creates a pool of connections for each of these nodes.
The size of the pool is configurable. For example, in the Java driver, one can define different pool sizes for nodes in local datacenter and for those in other (remote) datacenters.
If the number of connections in a pool go below this size for any reason, the driver immediately tries to fill the gap in the background.

## Using smart drivers with YugabyteDB Managed

Deploying applications
In a YugabyteDB Managed cluster, there is a load balancer sitting in front of the cluster nodes. The nodes are not directly accessible to the outside world. This is in contrast to the desired setup of the YCQL drivers which need direct access to all the nodes in a cluster.
The driver still works in such a setup but the applications lose out on some of the goodies of the driver like partition-aware query routing. The default retry policy of the driver too becomes ineffective in this case.

In case of YugabyteDB Managed cluster, the users can resort to VPC peering which will enable the client application to access all nodes of the cluster. This ensures that the application can leverage all the features of the driver.

If VPC peering is not possible, users can attempt to restore the retry capability by increasing the size of the connection pool (default size is 1 in Java driver) and providing a custom retry-policy to retry the failed operation on the same node.
There is no workaround for partition-aware query routing, though.

## Learn more

- [Build java applications using Apache Spark and YugabyteDB](/preview/integrations/apache-spark/java-ycql/) where YugabyteDB has forked a Cassandra Connector for Apache Spark.
