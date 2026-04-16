---
title: YugabyteDB smart drivers for YSQL
linkTitle: Smart drivers
description: Use YugabyteDB smart drivers to improve performance with connection load balancing for YSQL
headcontent: Manage connection load balancing automatically using smart drivers
menu:
  stable_develop:
    identifier: smart-drivers
    parent: drivers-orms
    weight: 400
type: docs
---

{{<tabs>}}
{{<tabitem href="../smart-drivers/" text="YSQL" icon="postgres" active="true">}}
{{<tabitem href="../smart-drivers-ycql/" text="YCQL" icon="cassandra" >}}
{{</tabs>}}

In addition to the compatible upstream PostgreSQL drivers, YugabyteDB also supports smart drivers, which extend PostgreSQL drivers to enable client applications to connect to YugabyteDB clusters without the need for external load balancers.

Yugabyte has developed the following smart drivers for YSQL, available as open source software under the Apache 2.0 license.

| GitHub project | Based on | Learn more |
| :--- | :--- | :--- |
| [YugabyteDB JDBC Driver for Java](https://github.com/yugabyte/pgjdbc) | PostgreSQL JDBC Driver | [Documentation](../java/yugabyte-jdbc/) |
| [YugabyteDB R2DBC Driver for Java](https://github.com/yugabyte/r2dbc-postgresql) | [PostgreSQL R2DBC driver](https://github.com/pgjdbc/r2dbc-postgresql) | [Documentation](../java/yb-r2dbc/) |
| [YugabyteDB PGX Driver for Go](https://github.com/yugabyte/pgx) | jackc/pgx | [Documentation](../go/yb-pgx/) |
| [YugabyteDB Psycopg2 Driver for Python](https://github.com/yugabyte/psycopg2) | PostgreSQL psycopg2 | [Documentation](../python/yugabyte-psycopg2/) |
| [YugabyteDB node-postgres Driver for Node.js](https://github.com/yugabyte/node-postgres) | node-postgres | [Documentation](../nodejs/yugabyte-node-driver/) |
| [YugabyteDB Npgsql Driver for C#](https://github.com/yugabyte/npgsql) | PostgreSQL Npgsql Driver | [Documentation](../csharp/ysql/) |
| [YugabyteDB Rust-postgres Driver](https://github.com/yugabyte/rust-postgres) | Rust-Postgres Driver | [Documentation](../rust/yb-rust-postgres) |
| [YugabyteDB Ruby Driver](https://github.com/yugabyte/ruby-pg) | ged/ruby-pg | [Documentation](../ruby/yb-ruby-pg) |

All YugabyteDB smart driver libraries are actively maintained, and receive bug fixes, performance enhancements, and security patches.

## Key features

YugabyteDB smart drivers have the following key features.

| Feature | Notes |
| :--- | :--- |
| Multiple hosts | As with the upstream driver (with the exception of node.js), you can specify multiple hosts for the initial connection, to avoid dropping connections in the case where the primary host is unavailable. |
| [Cluster aware](#cluster-aware-load-balancing) | Smart drivers perform automatic uniform connection load balancing<br/>After the driver establishes an initial connection, it fetches the list of available servers from the cluster and distributes connections evenly across these servers. |
| [Node type aware](#node-type-aware-load-balancing) | If your cluster has read replicas, distribute connections based on the node type (primary or read replica). (Not supported by all smart drivers.) |
| [Topology aware](#topology-aware-load-balancing) | If you want to restrict connections to particular geographies to achieve lower latency, you can target specific regions, zones, and fallback zones across which to balance connections. |
| [Configurable&nbsp;refresh interval](#servers-refresh-interval) | By default, the driver refreshes the list of available servers every five minutes. The interval is configurable. |
| [Multi-cluster](#connect-to-multiple-clusters) | Connect to more than one cluster from a single client application. |
| [Connection pooling](#connection-pooling) | Like the upstream driver, smart drivers support popular connection pooling solutions. |

## Overview

YugabyteDB is distributed, fault tolerant, and highly available. YugabyteDB automatically shards, replicates, and balances data across multiple nodes, which can be in different availability zones and regions. For better performance and fault tolerance, you can also balance application traffic (database connections) across the nodes in the universe to avoid excessive CPU and memory load on any single node.

You can load balance application connections to the database in the following ways:

- External load balancer
- Cluster-aware smart driver

### Using external load balancers

Because YugabyteDB is feature compatible with PostgreSQL, applications can use many of the widely available PostgreSQL client drivers to connect to a YugabyteDB cluster. However, these drivers are designed to be used with a monolithic database with a single network address. When they connect to a distributed database, they don't understand that the database consists of multiple nodes that they can connect to. One way to get around this limitation is to put the nodes behind one or more external load balancers.

However this approach results in complex configurations and increases management overhead. For example, the database cluster endpoints abstract role changes (primary elections) and topology changes (addition and removal of instances) occurring in the database cluster, and DNS updates are not instantaneous. In addition, they can lead to a slightly longer delay between the time a database event occurs and the time it's noticed and handled by the application.

![Connecting to a YugabyteDB cluster using external load balancers](/images/develop/smart-driver.png)

### Advantages of smart drivers

Smart client drivers allow applications to get better performance and fault tolerance by connecting to any node in a distributed SQL database cluster without the need for an external load balancer.

Smart drivers are optimized for use with a distributed SQL database, and are both cluster-aware and topology-aware; the driver keeps track of the members of the cluster as well as their locations. As nodes are added or removed from clusters, the driver updates its membership and topology information. The drivers read the database cluster topology from the metadata table, and route new connections to individual instance endpoints without relying on high-level cluster endpoints. The smart drivers are also capable of load balancing read-only connections across the available YB-TServers.

Smart drivers offer the following advantages over a PostgreSQL driver:

- Simplify operations by eliminating the load balancer. Because PostgreSQL drivers are designed for a single-node database, they do not keep track of the nodes of a distributed database cluster or their locations. Customers rely on external load balancers to route requests to different nodes in a database cluster, adding to the operational overhead. Smart drivers eliminate the need for an external load balancer.
- Improve performance by connecting to nearby nodes. Client applications can identify and connect to the database cluster nodes closest to them to achieve lower latency.
- Improve availability with better failure handling. If a database node becomes unreachable due to a network issue or server failure, clients can connect to a different node in the cluster. Retry logic on the client-side can make failures transparent to the end-user.

## Using YugabyteDB smart drivers

Developers can use smart driver connection load balancing in two configurations:

- Cluster-aware, using the **load balance** connection parameter
- Topology-aware, using the **topology keys** connection parameter

In both cases, the driver attempts to connect to the least loaded server from the available group of servers. For topology-aware load balancing, this group is determined by geo-locations specified using the topology keys connection parameter.

### Cluster-aware load balancing

With cluster-aware (also referred to as uniform) connection load balancing, connections are distributed uniformly across all the YB-TServers in the cluster, irrespective of their placement.

For example, if a client application creates a hundred connections to a YugabyteDB universe consisting of ten nodes, then the driver creates ten connections to each node. If the number of connections is not exactly divisible by the number of servers, then some servers may have one less or one more connection than the others. This is the client view of the load, so the servers may not be well-balanced if other client applications are not using a smart driver.

A connection works as follows:

- The driver makes an initial connection to the host specified in the URL or connection string. You can designate multiple hosts to act as backups if the connection to the primary host fails.
- The driver fetches information about the universe nodes using the `yb_servers()` function. By default, this list is refreshed every 5 minutes, and this time is checked when a new connection request is received.
- The driver then connects to the least-loaded node before returning the connection to the application.

#### Enable load balancing

To enable cluster-aware load balancing, you set the load balance connection parameter to `true` or `any` (the default is false) in the connection URL or the connection string (DSN style).

For example, using the _Go_ smart driver, you would enable load balancing as follows:

```go
"postgres://username:password@host:5433/database_name?load_balance=true"
```

(Note that the parameter names may be different depending on the driver you're using.)

With this parameter specified in the URL, the driver fetches and maintains a list of nodes from the given endpoint available in the YugabyteDB universe and distributes the connections equally across these nodes.

After the connection is established with a node, if that node fails, then the request is not retried.

For connections to be distributed equally, the application must use the same connection URL to create every connection it needs.

Note that, for load balancing, the nodes in the universe must be accessible. If, for example, the cluster has multiple regions deployed in separate VPCs, your application would need access to all the regions, typically via peering.

#### Node type-aware load balancing

If your cluster has read replicas, smart drivers can distribute (or exclude) connections based on the node type - primary or read replica.

To support this, the load balance property accepts the following additional values.

| Load&nbsp;balance&nbsp;setting | Description |
| :--- | :--- |
| any | Distribute connections equally across all nodes in the cluster, irrespective of type (primary or read replica). This is an alias for value _true_. |
| only-primary | Create connections equally across only primary nodes. If none are available, fail. |
| only-rr | Create connections equally across only read replica nodes. If none are available, fail. |
| prefer-primary | Create connections equally across primary nodes. If none are available, create them equally across the available read replica nodes. |
| prefer-rr | Create connections equally across read replica nodes. If none are available, create them equally across the available primary nodes. |

To see how nodes would be selected for connections using these values, see [Order of node selection for new connections](#order-of-node-selection-for-new-connections).

### Servers refresh interval

To change the frequency with which the driver fetches an updated list of servers, specify the server refresh interval parameter.

For example, using the _Go_ smart driver, you can change the interval to four minutes (specified in seconds) as follows:

```go
"postgres://username:password@host:5433/database_name?load_balance=true&yb_servers_refresh_interval=240"
```

(Note that the parameter names may be different depending on the driver you're using.)

Keep in mind that when a cluster is expanded, newly added nodes do not automatically start to receive client traffic. Your application must either explicitly request new connections or, if you are using a [pooling solution](#connection-pooling), you can configure the pooler to recycle connections periodically (for example, by setting maxLifetime and/or idleTimeout).

### Topology-aware load balancing

For a database deployment that spans multiple regions, evenly distributing requests across all database nodes may not be optimal. With topology-aware connection load balancing, you can target nodes in specified geo-locations. The driver then distributes connections uniformly among the nodes in the specified locations. This is beneficial in the following situations:

- For connecting to the geographically nearest regions and zones for lower latency and fewer network hops. Typically you would co-locate applications in the regions where your universe is located. Topology balancing allows you to target only regions where the applications are hosted.

- The universe has [preferred locations](../../../admin/yb-admin/#set-preferred-zones) assigned, where all the [tablet leaders](../../../architecture/docdb-sharding/sharding/) are hosted. In this case, for best performance you want your application to target the preferred locations.

You can also specify fallback locations, and the order in which they should be attempted. When no nodes are available in the primary location, the driver tries to connect to nodes in the fallback locations in the order specified. This way you can, for example, target the next geographically nearest location in case the first location is unavailable.

If you don't provide fallback locations, when no nodes are available in the primary locations, the driver falls back to nodes across the entire cluster.

#### Topology keys

You specify the locations as topology keys, with values in the format `cloud.region.zone`. Multiple zones can be specified as comma-separated values. You specify the topology keys in the connection URL or the connection string (DSN style). You still need to specify load balance as `true` to enable the topology-aware connection load balancing.

For example, using the _Go_ driver, you would set the parameters as follows:

```go
"postgres://username:password@localhost:5433/database_name?load_balance=true&topology_keys=cloud1.region1.zone1,cloud1.region1.zone2"
```

Use an asterisk (*) to specify all zones in a region. (You can't do this for region or cloud.) For example:

```go
"postgres://username:password@localhost:5433/database_name?load_balance=true&topology_keys=cloud1.region1.*"
```

#### Fallback topology keys

To specify fallback locations if a location is unavailable, add `:n` to the topology key, where `n` is an integer indicating priority. The following example using the _Go_ driver sets `zone1` as the topology key, and zones 2 and 3 as fallbacks (in that order) if `zone1` can't be reached:

```go
"postgres://username:password@localhost:5433/database_name?load_balance=true&topology_keys=cloud1.region1.zone1:1,cloud1.region1.zone2:2,cloud1.region1.zone3:3"
```

Not specifying a priority is the equivalent of setting priority to 1.

If you specify topology keys, you can additionally specify that connections only fall back to the nodes specified using the topology keys by setting the `fallback-to-topology-keys-only` property (JDBC smart driver only).

The property `fallback-to-topology-keys-only` is ignored when either `topology-keys` is empty, or when `load-balance` is set to `prefer-primary` or `prefer-rr`.

If no servers are available, the request may return with a failure.

### Order of node selection for new connections

Consider a hypothetical setup where a YugabyteDB cluster has nodes in a single region _region1_ of cloud _cloud1_ with three primary nodes across three zones (_zoneA_, _zoneB_, and _zoneC_); and three read replica nodes across zones _zoneB_, _zoneC_, and _zoneD_.

The following shows how nodes are selected for new connections, depending on the load balance and topology key settings.

(The property names used in the example are for the JDBC smart driver; use the equivalent for your preferred language.)

#### No topology keys

When `topology-keys` is not specified, nodes are selected as follows.

| Load balance setting | Connect to |
| :--- | :--- |
| true / any | Any nodes in entire cluster |
| only-primary | <ol><li>Primary nodes in entire cluster</li><li>Else, fail</li> |
| only-rr | <ol><li>Read Replica nodes in entire cluster</li><li>Else, fail</li> |
| prefer-primary | <ol><li>Primary nodes in entire cluster</li><li>Else, read replica nodes in entire cluster</li> |
| prefer-rr | <ol><li>Read Replica nodes in entire cluster</li><li>Else, primary nodes in entire cluster</li> |

#### Topology keys

When `topology-keys` is specified as `cloud1.region1.zoneA:1,cloud1.region1.zoneB:2`, nodes are selected as follows.

{{<tabpane text=true >}}
{{% tab header="Default" lang="fallback-false" %}}

If topology keys are specified, by default (that is, `fallback-to-topology-keys-only` is false), nodes are selected as follows.

| Load balance setting | Connect to |
| :--- | :--- |
| true / any | <ol><li>Any nodes in zoneA</li><li>Else, any nodes in zoneB</li><li>Else, any nodes in entire cluster |
| only-primary | <ol><li>Primary nodes in zoneA</li><li>Else, primary nodes in zoneB</li><li>Else, primary nodes in entire cluster</li><li>Else, fail |
| only-rr | <ol><li>Read replica nodes in zoneB</li><li>Else, read replica nodes in entire cluster</li><li>Else, fail |
| prefer-primary | <ol><li>Primary nodes in zoneA</li><li>Else, primary nodes in zoneB</li><li>Else, primary nodes in entire cluster</li><li>Else, read replica nodes in entire cluster |
| prefer-rr | <ol><li>Read replica nodes in zoneB</li><li>Else, read replica nodes in entire cluster</li><li>Else, primary nodes in entire cluster |

{{% /tab %}}
{{% tab header="Fall back to topology keys only" lang="fallback-true" %}}

If topology keys are specified, and `fallback-to-topology-keys-only` is true, nodes are selected as follows.

| Load balance setting | Connect to |
| :--- | :--- |
| true / any | <ol><li>Any nodes in zoneA</li><li>Else, any nodes in zoneB</li><li>Else, fail |
| only-primary | <ol><li>Primary nodes in zoneA</li><li>Else, primary nodes in zoneB</li><li>Else, fail |
| only-rr | <ol><li>Read replica nodes in zoneB</li><li>Else, fail |
| prefer-primary | fallback-to-topology-keys-only is ignored:<ol><li>Primary nodes in zoneA</li><li>Else, primary nodes in zoneB</li><li>Else, primary nodes in entire cluster</li><li>Else, read replica nodes in entire cluster |
| prefer-rr | fallback-to-topology-keys-only is ignored:<ol><li>Read replica nodes in zoneB</li><li>Else, read replica nodes in entire cluster</li><li>Else, primary nodes in entire cluster |

{{% /tab %}}
{{</tabpane >}}

### Connect to multiple clusters

For deployments where a single client application needs to connect to more than one YugabyteDB cluster (for example, xCluster or two completely independent clusters), the smart driver can connect to multiple clusters while keeping connections balanced in each connected cluster.

Note that connections are not balanced across multiple clusters; rather, the smart driver maintains multiple connection pools, but each is isolated.

The smart driver identifies the cluster from the URL used to connect, and ensures load in the cluster is evenly balanced. You don't need to specify any additional properties.

For example:

```java
String url1Cluster1 = "jdbc:yugabytedb://<host-in-cluster1>:5433/database_name?load-balance=true";
String url1Cluster2 = "jdbc:yugabytedb://<host-in-cluster2>:5433/database_name?load-balance=only-primary&topology-keys=aws.us-east-1.*";

String url2Cluster1 = "jdbc:yugabytedb://<another-host-in-cluster1>:5433/database_name?load-balance=true";
```

The application can initiate connections using any of the URLs, and the driver will ensure the connection goes to the least loaded server in the respective cluster.

Multi-cluster is supported in YugabyteDB v2024.1.6.0, v2024.2.3.0, and v2.25.2.0. You can also include URLs from a _maximum of one_ cluster running an older (unsupported) version of YugabyteDB. Using the preceding example, _Cluster1_ could be running an unsupported version, but not both _Cluster1_ and _Cluster2_.

Currently, multi-cluster connections are available only in the Java (JDBC) smart driver (v42.7.3-yb-4 or later).

## Connection pooling

Smart drivers work seamlessly with [YSQL Connection Manager](../../../additional-features/connection-manager-ysql/).

Smart drivers can also be configured with popular third-party pooling solutions such as Hikari and Tomcat. Different pools can be configured with different load balancing policies if required. For example, an application can configure one pool with topology awareness for one region and its availability zones, and configure another pool to communicate with a completely different region.

The appropriate connection timeout depends on the specific requirements of the application. In addition to the usual considerations, because YugabyteDB is distributed, you also want connections to move to recovered or newly-added nodes as quickly as possible.

When a connection reaches the timeout period, the pool re-establishes a new connection to the node with the least amount of connections, which would likely be the new node. Set the timeout too long, and you risk not taking maximum advantage of a new node. For example, a timeout of 10 minutes means a new node might not receive connections for up to 10 minutes. (The node will still be used for YB-TServer operations, but not for new client connections.) Setting the timeout too short, however, degrades overall latency performance due to the high first connection latency. Experiment with different timeout values and monitor the performance of the application and the database to determine the optimal value.

For an example of how connection pooling reduces latencies, see [Database Connection Management: Exploring Pools and Performance](https://www.yugabyte.com/blog/database-connection-management/).

## Using smart drivers with YugabyteDB Aeon

[YugabyteDB Aeon](/stable/yugabyte-cloud/) clusters automatically use the uniform load balancing provided by the cloud provider where the cluster is provisioned. YugabyteDB Aeon creates an external load balancer to distribute the load across the nodes in a particular region. For multi-region clusters, each region has its own external load balancer.

When connecting using an upstream driver, you connect to the region of choice, and application connections are then uniformly distributed across the region without the need for any special coding.

If you are using a smart driver, you can connect to any region and the load balancer acts as a discovery endpoint, allowing the application to use connections to nodes in all regions.

YugabyteDB Aeon clusters also support topology-aware load balancing. If the cluster has a [preferred region](/stable/yugabyte-cloud/cloud-basics/create-clusters/create-clusters-multisync/#preferred-region), set the topology keys to a zone in that region for best performance.

### Deploying applications

To take advantage of smart driver load balancing features when connecting to clusters in YugabyteDB Aeon, applications using smart drivers must be deployed in a VPC that has been [peered with the cluster VPC](/stable/yugabyte-cloud/cloud-basics/cloud-vpcs/cloud-add-peering/). For applications that access the cluster from outside the peered network or using private endpoints via a private link, set the load balance connection parameter to `false`; in this case, the cluster performs the load balancing.

Applications that use smart drivers from outside the peered network with load balance on will try to connect to the inaccessible nodes before falling back to the upstream driver behavior. You may see a warning similar to the following:

```output
WARNING [com.yug.Driver] (agroal-11) Failed to apply load balance. Trying normal connection
```

This indicates that the smart driver was unable to perform smart load balancing. To avoid the added latency incurred, turn load balance off.

For information on VPC peering in YugabyteDB Aeon, refer to [VPC network](/stable/yugabyte-cloud/cloud-basics/cloud-vpcs/).

### SSL/TLS verify-full support

YugabyteDB Aeon requires TLS/SSL. Depending on the smart driver, using load balancing with a cluster in YugabyteDB Aeon and SSL mode verify-full may require additional configuration. The following table describes support for verify-full for YugabyteDB smart drivers.

| Smart&nbsp;Driver | Support | Notes |
| :--- | :--- | :--- |
| Java | Yes | Set the `sslhostnameverifier` connection parameter to `com.yugabyte.ysql.YBManagedHostnameVerifier`. |
| Python | No | Use verify-ca or the upstream psycopg2 driver. |
| Go | Yes | |
| Node.js | Yes | In the ssl object, set `rejectUnauthorized` to true, `ca` to point to your cluster CA certificate, and `servername` to the cluster host name. |

For more information on using TLS/SSL in YugabyteDB Aeon, refer to [Encryption in transit](/stable/yugabyte-cloud/cloud-secure-clusters/cloud-authentication/).

## Learn more

- YugabyteDB Friday Tech Talk: [PostgreSQL Smart Drivers](https://youtu.be/FbXrRdB_4u0)
- [Smart driver FAQ](/stable/faq/smart-drivers-faq/)
- Smart driver [architecture documentation](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/smart-driver.md)
- Blog: [Node.js Smart Drivers for YugabyteDB: Why You Should Care](https://www.yugabyte.com/blog/node-js-smart-drivers-for-yugabytedb/)
