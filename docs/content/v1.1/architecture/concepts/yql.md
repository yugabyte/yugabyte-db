---
title: YQL Query Layer
linkTitle: YQL Query Layer
description: YugaByte Query Layer (YQL)
menu:
  v1.1:
    identifier: architecture-yql
    parent: architecture-concepts
    weight: 935
---

The YQL layer implements the server-side of multiple protocols/APIs that YugaByte DB supports. Currently, YugaByte DB supports YCQL and YEDIS, 2 transactional NoSQL APIs, and YSQL, a distributed SQL API. The 2 NoSQL APIs are wire compatible with Cassandra Query Language (CQL) and Redis while the SQL API is wire compatible with PostgreSQL.

![cluster_overview](/images/architecture/cluster_overview.png)

Every YB-TServer is configured to support these protocols, on different ports. Port 9042 is the default port for CQL-compatible protocol and 6379 is the default port used for the Redis-compatible protocol. Port 5433 is used for the PostgreSQL-compatible protocol.

From the application perspective this is a stateless layer and the clients can connect to any (one or more) of the YB-TServers on the appropriate port to perform operations against the YugaByte DB cluster.

The YQL layer running inside each YB-TServer implements some of the API specific aspects of each support API (such as YCQL or YEDIS or YSQL), but ultimately replicates/stores/retrieves/replicates data using DocDB, YugaByte DB’s common underlying strongly-consistent & distributed store. Some of the sub-components in YQL are:

- A CQL compiler and execution layer - This component includes a “statement cache”, a cache for compiled/execution plan for prepared statements to avoid overheads associated with repeated parsing of CQL statements.
- A Redis command parser and execution layer
- Support for language specific builtin operations, data type encodings, etc.


