---
title: YugabyteDB smart drivers for YSQL
linkTitle: Smart drivers
description: Use YugabyteDB smart drivers to improve performance with connection load balancing for YSQL
headcontent: Manage connection load balancing automatically using smart drivers
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    identifier: smart-drivers
    parent: drivers-orms
    weight: 400
type: docs
---

In addition to the compatible upstream PostgreSQL drivers, YugabyteDB also supports **smart drivers**, which extend the PostgreSQL drivers to enable client applications to connect to YugabyteDB clusters without the need for external load balancers. YugabyteDB smart drivers have the following features:

- **Cluster-aware**. Drivers know about all the data nodes in a YugabyteDB cluster, eliminating the need for an external load balancer.
- **Topology-aware**. For geographically-distributed applications, the driver can seamlessly connect to the geographically nearest regions and availability zones for lower latency.

All YugabyteDB smart driver libraries are actively maintained, and receive bug fixes, performance enhancements, and security patches.

## Overview

YugabyteDB is a distributed, fault tolerant and highly available database with low latencies for reads and writes. Data in YugabyteDB is automatically sharded, replicated, and balanced across multiple nodes that can potentially be in different availability zones and regions. For better performance and fault tolerance, you can also balance application traffic (that is, connections to the database) across the nodes in the cluster to avoid excessive load (CPU and memory) on any single node (that is, hot nodes).

You can load balance application connections to the database in the following ways:

- External load balancer
- Cluster-aware smart driver

### External load balancer

Because YugabyteDB is feature compatible with PostgreSQL, applications can use many of the widely available PostgreSQL client drivers to connect to a YugabyteDB cluster. However, these drivers are designed to be used with a monolithic database with a single network address. When they connect to a distributed database, they don't understand that the database consists of multiple nodes that they can connect to. One way to get around this limitation is to put the nodes behind one or more external load balancers.

However this approach results in complex configurations and increases management overhead. For example, the database cluster endpoints abstract role changes (primary elections) and topology changes (addition and removal of instances) occurring in the database cluster, and DNS updates are not instantaneous. In addition, they can lead to a slightly longer delay between the time a database event occurs and the time it's noticed and handled by the application.

![Connecting to a YugabyteDB cluster using external load balancers](/images/develop/smart-driver.png)

### Smart drivers

Smart client drivers allow applications to get better performance and fault tolerance by connecting to any node in a distributed SQL database cluster without the need for an external load balancer.

Smart drivers are optimized for use with a distributed SQL database, and are both cluster-aware and topology-aware; the driver keeps track of the members of the cluster as well as their locations. As nodes are added or removed from clusters, the driver updates its membership and topology information. The drivers read the database cluster topology from the metadata table, and route new connections to individual instance endpoints without relying on high-level cluster endpoints. The smart drivers are also capable of load balancing read-only connections across the available YB-TServers.

Smart drivers offer the following advantages over a PostgreSQL driver:

- Simplify operations by eliminating the load balancer. Because PostgreSQL drivers are designed for a single-node database, they do not keep track of the nodes of a distributed database cluster or their locations. Customers rely on external load balancers to route requests to different nodes in a database cluster, adding to the operational overhead. Smart drivers eliminate the need for an external load balancer.
- Improve performance by connecting to nearby nodes. Client applications can identify and connect to the database cluster nodes closest to them to achieve lower latency.
- Improve availability with better failure handling. If a database node becomes unreachable due to a network issue or server failure, clients can connect to a different node in the cluster. Retry logic on the client-side can make failures transparent to the end-user.

## Using YugabyteDB smart drivers

Yugabyte has developed the following smart drivers, available as open source software under the Apache 2.0 license.

| GitHub project | Based on | Learn more |
| :--- | :--- | :--- |
| [YugabyteDB JDBC Driver for Java](https://github.com/yugabyte/pgjdbc) | PostgreSQL JDBC Driver | [Documentation](../java/yugabyte-jdbc/) |
| [YugabyteDB PGX Driver for Go](https://github.com/yugabyte/pgx) | jackc/pgx | [Documentation](../go/yb-pgx/) |
| [YugabyteDB Psycopg2 Driver for Python](https://github.com/yugabyte/psycopg2) | PostgreSQL psycopg2 | [Documentation](../python/yugabyte-psycopg2/) |
| [YugabyteDB node-postgres Driver for Node.js](https://github.com/yugabyte/node-postgres) | node-postgres | [Documentation](../nodejs/yugabyte-node-driver/) |

Developers can use smart driver connection load balancing in two configurations:

- Cluster aware
- Topology aware

### Cluster-aware connection load balancing

With cluster-aware connection load balancing, connections are distributed uniformly across all the YB-TServers in the cluster, irrespective of their placement.

For example, if a client application creates 100 connections to a YugabyteDB cluster consisting of 10 nodes, then the driver creates 10 connections to each node. If the number of connections is not exactly divisible by the number of servers, then a few may have 1 less or 1 more connection than the others. This is the client view of the load, so the servers may not be well balanced if other client applications are not using the Smart driver.

To enable cluster-aware load balancing, you set the load balance parameter to true in the connection URL or the connection string (DSN style).

For example, using the Go driver, you would set the parameter as follows:

```go
"postgres://username:password@localhost:5433/database_name?load_balance=true"
```

With this parameter specified in the URL, the driver fetches and maintains a list of nodes from the given endpoint (localhost in preceding example) available in the YugabyteDB cluster and distributes the connections equally across them.

This list is refreshed every 5 minutes, when a new connection request is received.

The application must use the same connection URL to create every connection it needs, so that the distribution happens equally.

### Topology-aware connection load balancing

With topology-aware connection load balancing, you can target nodes in specified geo-locations. The driver then distributes connections uniformly among the nodes in the specified locations. If no servers are available, the request may return with a failure.

You specify the locations as topology keys, with values in the format `cloud.region.zone`. Multiple zones can be specified as comma-separated values. You specify the topology keys in the connection URL or the connection string (DSN style).

You still need to specify load balance as true to enable the topology-aware connection load balancing.

For example, using the Go driver, you would set the parameters as follows:

```go
"postgres://username:password@localhost:5433/database_name?load_balance=true&topology_keys=cloud1.region1.zone1,cloud1.region1.zone2"
```

## Using smart drivers with YugabyteDB Managed

[YugabyteDB Managed](../../yugabyte-cloud/) clusters automatically use the uniform load balancing provided by the cloud provider where the cluster is provisioned. YugabyteDB Managed creates an external load balancer to distribute the load across the nodes in a particular region. For multi-region clusters, each region has its own external load balancer.

For regular connections, you need to connect to the region of choice, and application connections are then uniformly distributed across the region without the need for any special coding.

If you are using a smart driver with topology awareness, you can connect to any region and the load balancer acts as a discovery endpoint, allowing the application to use connections to nodes in all regions.

Applications using smart drivers must be deployed in a VPC that has been peered with the cluster VPC. For information on VPC networking in YugabyteDB Managed, refer to [VPC network](../../yugabyte-cloud/cloud-basics/cloud-vpcs/).

For applications that access the cluster from a non-peered network, use the upstream PostgreSQL driver instead; in this case, the cluster performs the load balancing. Applications that use smart drivers from non-peered networks fall back to the upstream driver behavior automatically.

YugabyteDB Managed requires TLS/SSL. For more information on using TLS/SSL in YugabyteDB Managed, refer to [Encryption in transit](../../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/).

## FAQ

### What is a smart driver?

Think of smart drivers as PostgreSQL drivers with the addition of "smart" features that take advantage of the distributed nature of YugabyteDB. Depending on how it is configured, a smart driver intelligently distributes application connections across the nodes and regions of a YugabyteDB cluster, providing lower latencies and preventing hot nodes.

### Why do I need connection load balancing?

While upstream PostgreSQL drivers are compatible with YugabyteDB, they create all connections on the same server. This results in all the load being handled by a single node, when it could be spread across multiple nodes.

Topology-aware load balancing further achieves lower latencies by enabling applications to identify and connect to the database cluster nodes closest to them.

### When should I use a smart driver?

YugabyteDB - Use a smart driver if all the nodes in the cluster are available for direct connectivity from the location where the client application is running.

YugabyteDB Managed - Use a smart driver if your client application is running in a peered VPC. Without a smart driver, YugabyteDB Managed falls back to the connection load balancing provided by cloud providers; however you lose many of the advantages of cluster- and topology-awareness provided by the smart drivers.

### How hard is it to port an application to use a smart driver?

Porting an application that already uses a PostgreSQL driver is straightforward. No application-level or intrusive changes are required.

Different language drivers initialize connections in different ways, but in all cases porting to a smart driver requires adding the load balance and (optionally) topology keys properties to the connection code. This amounts to changing the connection URL to add the properties, or a minor code change to pass in the new properties.

For example, In JDBC, you change the URL to use the load balance property:

```java
string yburl = "jdbc://yugabytedb://hostname:port/database?user=yugabyte&password=yugabyte&load-balance=true"
DriverManager.getConnection(yburl);
```

### How does the smart driver determine if a node is unhealthy, or the cluster configuration has changed?

The driver executes a query to find out all the healthy nodes whenever a new connection is being created and if the information it has is at least 5 minutes old.

If a server becomes healthy again, it is added to the list of healthy nodes. The output contains all the live healthy nodes regardless of whether they are new or old.

Note that active connections on a particular server/endpoint are not repaired automatically. Your application should have code to handle dropped connections.

### Does the driver know when a region fails?

The driver is not directly aware of region or zone failures. However, it is aware of which nodes are healthy. If an entire region or zone is unavailable, no new connections are made to the zone or region until the nodes reappear in the list of healthy nodes.

### Does the driver provide metrics that can be monitored via JMX?

No.

### Are there recommended settings for the maximum lifetime of a connection?

Apart from directing connections to healthy nodes, smart driver connections are no different. Smart drivers require no special optimizations or modifications to your application's connection handling.

## Learn more

Smart driver [architecture documentation](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/smart-driver.md)
