---
title: Overview of YugabyteDB Query Layer
headerTitle: Overview of YugabyteDB Query Layer
linkTitle: Overview
description: The YugabyteDB Query Layer is the upper layer of YugabyteDB. Applications interact directly with YQL using client drivers.
menu:
  stable:
    identifier: architecture-query-layer-yql
    parent: architecture-query-layer
    weight: 1172
type: docs
---

The YugabyteDB Query Layer (YQL) is the upper layer of YugabyteDB. Applications interact directly with YQL using client drivers. This layer deals with the API-specific aspects such as query and command compilation, as well as the runtime functions such as data type representations, built-in operations, and so on. YQL is designed with extensibility in mind, allowing for new APIs to be added. Currently, YQL supports two types of distributed SQL APIs: [YSQL](../../../api/ysql/) and [YCQL](../../../api/ycql/).

![cluster_overview](/images/architecture/cluster_overview.png)

As per the preceding diagram, every YB-TServer is configured to support both types of query languages on different ports: port 5433 is the default port for YSQL and 9042 is the default port for YCQL.

From the application perspective, YQL is stateless and the clients can connect to one or more YB-TServers on the appropriate port to perform operations against a YugabyteDB cluster.

The YQL inside of each YB-TServer implements some API-specific aspects required for each of the supported APIs, but ultimately it is responsible for replication storage, and retrieval of data using DocDB, which is YugabyteDB’s common underlying strongly-consistent and distributed store. The following are some of the subcomponents in YQL for each API:

- A statement cache that caches compiled or execution plans for prepared statements to avoid overheads associated with repeated parsing of statements.
- A command parser and execution layer.
- Language-specific built-in operations, data type encodings, and so on.
