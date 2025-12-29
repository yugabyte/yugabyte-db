---
title: Smart driver FAQ
linkTitle: Smart driver FAQ
description: YugabyteDB smart drivers frequently asked questions
aliases:
  - /stable/develop/drivers-orms/smart-drivers-faq/
menu:
  stable_faq:
    identifier: smart-drivers-faq
    parent: faq
    weight: 40
type: docs
unversioned: true
---

### What is a smart driver?

Think of smart drivers as PostgreSQL drivers with the addition of "smart" features that take advantage of the distributed nature of YugabyteDB. A smart driver intelligently distributes application connections across the nodes and regions of a YugabyteDB cluster, without the need for external load balancers. Balanced connections provide lower latencies and prevent hot nodes. For geographically-distributed applications, the driver can seamlessly connect to the geographically nearest regions and availability zones for lower latency.

{{<lead link="/stable/develop/drivers-orms/smart-drivers/">}}
YugabyteDB smart drivers for YSQL
{{</lead>}}

### What languages are supported?

YugabyteDB smart drivers for YSQL are currently available for the following languages:

- Java
- Go
- Python
- Node.js
- C#
- Rust
- Ruby

### Why do I need connection load balancing?

While upstream PostgreSQL drivers are compatible with YugabyteDB, they create all connections on the same server. This results in all the load being handled by a single node, when it could be spread across multiple nodes.

Topology-aware load balancing further achieves lower latencies by enabling applications to identify and connect to the database cluster nodes closest to them.

### When should I use a smart driver?

- YugabyteDB - Use a smart driver if all the nodes in the cluster are available for direct connectivity from the location where the client application is running. For example, if the VPC hosting YugabateDB is peered with the VPC hosting the application.

- YugabyteDB Aeon - Use a smart driver if your client application is running in a peered VPC. Without a smart driver, YugabyteDB Aeon falls back to the connection load balancing provided by cloud providers; however you lose many of the advantages of cluster- and topology-awareness provided by the smart drivers.

If the external address given in the connection URL and individual nodes are not accessible directly, do not enable smart driver load balancing. Applications that use smart drivers from outside the peered network with load balance on will try to connect to the inaccessible nodes before falling back to the upstream driver behavior. You may see a warning similar to the following:

```output
WARNING [com.yug.Driver] (agroal-11) Failed to apply load balance. Trying normal connection
```

This indicates that the smart driver was unable to perform smart load balancing. To avoid the added latency incurred, turn load balance off or use the upstream driver.

### How hard is it to port an application to use a smart driver?

Porting an application that already uses a PostgreSQL driver is straightforward. No application-level or intrusive changes are required.

Different language drivers initialize connections in different ways, but in all cases porting to a smart driver requires adding the load balance and (optionally) topology keys properties to the connection code. This amounts to changing the connection URL to add the properties, or a minor code change to pass in the new properties.

For example, In JDBC, you change the URL to use the load balance property:

```java
String yburl = "jdbc:yugabytedb://hostname:port/database?user=yugabyte&password=yugabyte&load-balance=true";
DriverManager.getConnection(yburl);
```

### How does the smart driver determine if a node is unhealthy, or the cluster configuration has changed?

The driver executes a query to find out all the healthy nodes whenever a new connection is being created and if the information it has is at least 5 minutes old.

If a server becomes healthy again, it is added to the list of healthy nodes. The output contains all the live healthy nodes regardless of whether they are new or old.

Note that active connections on a particular server/endpoint are not repaired automatically. Your application should have code to handle dropped connections.

### Do smart drivers know when a region fails?

The driver is not directly aware of region or zone failures. However, it is aware of which nodes are healthy. If an entire region or zone is unavailable, no new connections are made to the zone or region until the nodes reappear in the list of healthy nodes.

### Do smart drivers provide metrics that can be monitored via JMX?

No.

### Are there recommended settings for the maximum lifetime of a connection?

Apart from directing connections to healthy nodes, smart driver connections are no different. Smart drivers require no special optimizations or modifications to your application's connection handling.

### How do I limit traffic to specific clouds, regions, or AZs using a smart driver?

You can direct connections to specific clouds, regions, or AZs using topology keys. Each smart driver client sets this using a parameter particular to each language, as follows:

- Java: `topology-keys`
- Node: `topologyKeys`
- C#: `Topology Keys`
- Go: `topology_keys`
- Python: `topology_keys`
- Rust: `topology_keys`
- Ruby: `topology_keys`

{{<lead link="/stable/develop/drivers-orms/smart-drivers/#topology-aware-load-balancing">}}
Topology-aware connection load balancing
{{</lead>}}
