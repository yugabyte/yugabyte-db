---
title: Best practices for YSQL clients
headerTitle: Best practices for YSQL clients
linkTitle: YSQL clients
description: Tips and tricks for administering YSQL clients
headcontent: Tips and tricks for administering YSQL clients
menu:
  stable_develop:
    identifier: best-practices-ysql-clients
    parent: best-practices-develop
    weight: 20
type: docs
---

Client-side configuration plays a critical role in the performance, scalability, and resilience of YSQL applications. This guide highlights essential best practices for managing connections, balancing load across nodes, and handling failovers efficiently using YugabyteDB's smart drivers and connection pooling. Whether you're deploying in a single region or across multiple data centers, these tips will help ensure your applications make the most of YugabyteDB's distributed architecture

## Load balance and failover using smart drivers

YugabyteDB [smart drivers](/stable/develop/drivers-orms/smart-drivers/) provide advanced cluster-aware load-balancing capabilities that enable your applications to send requests to multiple nodes in the cluster by connecting to one node. You can also set a fallback hierarchy by assigning priority to specific regions and ensuring that connections are made to the region with the highest priority, and then fall back to the region with the next priority in case the high-priority region fails.

{{<lead link="https://www.yugabyte.com/blog/multi-region-database-deployment-best-practices/#load-balancing-with-smart-driver">}}
For more information, see [Load balancing with smart drivers](https://www.yugabyte.com/blog/multi-region-database-deployment-best-practices/#load-balancing-with-smart-driver).
{{</lead>}}

## Make sure the application uses new nodes

When a cluster is expanded, newly added nodes do not automatically start to receive client traffic. Regardless of the language of the driver or whether you are using a smart driver, the application must either explicitly request new connections or, if it is using a pooling solution, it can configure the pooler to recycle connections periodically (for example, by setting maxLifetime and/or idleTimeout).

## Scale your application with connection pools

Set up different pools with different load balancing policies as needed for your application to scale by using popular pooling solutions such as HikariCP and Tomcat along with YugabyteDB [smart drivers](/stable/develop/drivers-orms/smart-drivers/).

{{<lead link="/stable/develop/drivers-orms/smart-drivers/#connection-pooling">}}
For more information, see [Connection pooling](/stable/develop/drivers-orms/smart-drivers/#connection-pooling).
{{</lead>}}

### Database migrations and connection pools

In some cases, connection pools may trigger unexpected errors while running a sequence of database migrations or other DDL operations.

Because YugabyteDB is distributed, it can take a while for the result of a DDL to fully propagate to all caches on all nodes in a cluster. As a result, after a DDL statement completes, the next DDL statement that runs right afterwards on a different PostgreSQL connection may, in rare cases, see errors such as `duplicate key value violates unique constraint "pg_attribute_relid_attnum_index"` (see issue {{<issue 12449>}}). It is recommended to use a single connection while running a sequence of DDL operations, as is common with application migration scripts with tools such as Flyway or Active Record.

## Use YSQL Connection Manager

{{<tags/feature/ea idea="1368">}}YugabyteDB includes a built-in connection pooler, YSQL Connection Manager, which provides the same connection pooling advantages as other external pooling solutions, but without many of their limitations. As the manager is bundled with the product, it is convenient to manage, monitor, and configure the server connections.

For more information, refer to the following:

- [YSQL Connection Manager](../../../additional-features/connection-manager-ysql/)
- [Built-in Connection Manager Turns Key PostgreSQL Weakness into a Strength](https://www.yugabyte.com/blog/connection-pooling-management/)
