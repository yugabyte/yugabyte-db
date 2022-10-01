---
title: Smart driver FAQ
linkTitle: FAQ
description: YugabyteDB smart drivers frequently asked questions
headcontent: Frequently asked questions about YugabyteDB smart drivers
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    identifier: smart-drivers-faq
    parent: smart-drivers
    weight: 100
type: docs
---

### What is a smart driver?

Think of smart drivers as PostgreSQL drivers with the addition of "smart" features that take advantage of the distributed nature of YugabyteDB. Depending on how it is configured, a smart driver intelligently distributes application connections across the nodes and regions of a YugabyteDB cluster, providing lower latencies and preventing hot nodes.

### Why do I need connection load balancing?

While upstream PostgreSQL drivers are compatible with YugabyteDB, they create all connections on the same server. This results in all the load being handled by a single node, when it could be spread across multiple nodes.

Topology-aware load balancing further achieves lower latencies by enabling applications to identify and connect to the database cluster nodes closest to them.

### When should I use a smart driver?

**YugabyteDB** - Use a smart driver if all the nodes in the cluster are available for direct connectivity from the location where the client application is running.

**YugabyteDB Managed** - Use a smart driver if your client application is running in a peered VPC. Without a smart driver, YugabyteDB Managed falls back to the connection load balancing provided by cloud providers; however you lose many of the advantages of cluster- and topology-awareness provided by the smart drivers.

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

### Do smart drivers know when a region fails?

The driver is not directly aware of region or zone failures. However, it is aware of which nodes are healthy. If an entire region or zone is unavailable, no new connections are made to the zone or region until the nodes reappear in the list of healthy nodes.

### Do smart drivers provide metrics that can be monitored via JMX?

No.

### Are there recommended settings for the maximum lifetime of a connection?

Apart from directing connections to healthy nodes, smart driver connections are no different. Smart drivers require no special optimizations or modifications to your application's connection handling.
