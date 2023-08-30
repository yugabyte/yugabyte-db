---
title: YCQL features
headerTitle: YCQL features
linkTitle: YCQL features
description: Explore core features in YCQL
headcontent: Explore core features in YCQL
image: /images/section_icons/api/ycql.png
menu:
  stable:
    identifier: explore-ycql-language
    parent: explore
    weight: 900
type: indexpage
showRightNav: true
---
YugabyteDB's [YCQL API](../../api/ycql/) has its roots in the [Cassandra Query Language (CQL)](http://cassandra.apache.org/doc/latest/cql/index.html) and runs on top of YugabyteDB's distributed storage layer called DocDB. This architecture allows YCQL to support most Cassandra features, such as data types, queries, expressions, operators and so on and at the same providing seamless scalability and resilience.

{{< tip title="Tip" >}}
A large portion of the documentation and examples written for Cassandra would work against YCQL.
{{< /tip >}}

## Cassandra features in YCQL

The following table lists the most important YCQL features which you would find familiar if you have worked with Cassandra.

| YCQL Feature | Description |
| :----------- | :---------- |
| [Data definition](../../api/ycql/#ddl-statements) | YCQL shell with `ycqlsh`, keysapces, tables, indexes and types |
| [Data Types](../../api/ycql/#data-types) | String, numeric, frozen types, UUID, JSONB ... |
| [Data Manipulation](../../api/ycql/#dml-statements) | `SELECT`, `INSERT`, `UPDATE`, `DELETE` ... |
| [Expressions](../../api/ycql/#expressions) | Simple Values, Function calls, Subscript ... |
| [Operators](../../api/ycql/expr_ocall/)| Binary, Unary, Null operators ... |
| [Security](../../api/ycql/#ddl-security-statements) | Roles and Permissions |

## Going beyond Cassandra

YCQL has a number of features that are not present in Cassandra, as summarized in the following table.

| YCQL Feature | Description |
| :----------- | :---------- |
| [Strongly consistent with RAFT replication](../../architecture/docdb-replication/replication/#raft-replication) | Enables strong consistency across replicas |
| [Fast transactions](../../architecture/transactions/distributed-txns/) | Single round-trip distributed transactions |
| [Native JSONB support](../../api/ycql/type_jsonb)| Enables document data modelling like MongoDB |
| [Fast and consistent Secondary Indexes](../../explore/indexes-constraints/secondary-indexes-ycql) | Immediately consistent indexes with point lookups (no fan-out) |
| [Secondary indexes with JSONB datatype](../../explore/indexes-constraints/secondary-indexes-with-jsonb-ycql) | Efficient reads with consistency and flexibility in data model |

## Learn more

- [Comparison with Apache Cassandra](../../faq/comparisons/cassandra)
- [YCQL command reference](../../api/ycql/)
- [Cassandra 3.4 Feature parity](cassandra-feature-support)
