---
title: Overview
headerTitle: Overview of Yugabyte Query Layer (YQL)
linkTitle: Overview
description: The Yugabyte Query Layer (YQL) is the upper layer of YugabyteDB. Applications interact directly with YQL using client drivers.
aliases:
  - /architecture/concepts/yql/
menu:
  latest:
    identifier: architecture-query-layer-yql
    parent: architecture-query-layer
    weight: 1172
---

The Yugabyte Query Layer (YQL) is the upper layer of YugabyteDB. Applications interact directly with YQL using client drivers. This layer deals with the API specific aspects such as query/command compilation and the run-time (data type representations, built-in operations and more). YQL is built with extensibility in mind, and allows for new APIs to be added. Currently, YQL supports two flavors of distributed SQL APIs, namely [YSQL](../../../api/ysql) and [YCQL](../../../api/ycql).

![cluster_overview](/images/architecture/cluster_overview.png)

Every YB-TServer is configured to support these protocols, on different ports. Port 5433 is the default port for YSQL and 9042 is the default port for YCQL.

From the application perspective this is a stateless layer and the clients can connect to any (one or more) of the YB-TServers on the appropriate port to perform operations against the YugabyteDB cluster.

The YQL layer running inside each YB-TServer implements some of the API specific aspects of each support API, but ultimately replicates/stores/retrieves/replicates data using DocDB, YugabyteDB’s common underlying strongly-consistent and distributed store. Some of the subcomponents in YQL for each API are:

- A “statement cache”, a cache for compiled/execution plan for prepared statements to avoid overheads associated with repeated parsing of statements.
- A command parser and execution layer
- Support for language specific builtin operations, data type encodings, etc.


