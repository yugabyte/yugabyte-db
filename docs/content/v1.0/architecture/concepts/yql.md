---
title: Query Layer
linkTitle: Query Layer
description: YugaByte Query Layer (YQL)
menu:
  v1.0:
    identifier: architecture-yql
    parent: architecture-concepts
    weight: 970
---

The YQL layer implements the server-side of multiple protocols/APIs that YugaByte supports.
Currently, YugaByte supports Apache Cassandra, Redis, PostgreSQL(beta) wire-protocols natively, and other SQL services are in the roadmap.

Every YB-TServer is configured to support these protocols, on different ports. Port 9042 is the
default port for CQL wire protocol and 6379 is the default port used for Redis wire protocol.

From the application perspective this is a stateless layer and the clients can connect to any (one
or more) of the YB-TServers on the appropriate port to perform operations against the YugaByte
cluster.

The YQL layer running inside each YB-TServer implements some of the API specific aspects of each
support API (such as CQL or Redis), but ultimately replicates/stores/retrieves/replicates data using
YugaByte’s common underlying strongly-consistent & distributed core (we refer to as YBase). Some of
the sub-components in YQL are:

- A CQL compiler and execution layer - This component includes a “statement cache”, a cache for compiled/execution plan for prepared statements to avoid overheads associated with repeated parsing of CQL statements.
- A Redis command parser and execution layer
- Support for language specific builtin operations, data type encodings, etc.

![cluster_overview](/images/architecture/cluster_overview.png)
