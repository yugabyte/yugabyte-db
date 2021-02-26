---
title: YSQL vs PostgreSQL
headerTitle: YSQL vs PostgreSQL
linkTitle: YSQL vs PostgreSQL
description: PostgreSQL features in YSQL.
headcontent: PostgreSQL features in YSQL.
image: /images/section_icons/api/ysql.png
menu:
  latest:
    identifier: explore-ysql-language-features
    parent: explore
    weight: 200
isTocNested: true
showAsideToc: true
---

The YSQL API of YugabyteDB reuses a fork of the query layer of PostgreSQL as its starting point and runs on top of YugabyteDB’s distributed storage layer called DocDB. This architectural decision allows YSQL to support most of the PostgreSQL features such as data types, queries, expressions, operators and functions, stored procedures, triggers, extensions, and so on, all of which are expected to work identically on both database systems.

{{< tip title="Tip" >}}
A large portion of the documentation and examples written for PostgreSQL would work against YSQL.

{{< /tip >}}

The following diagram demonstrates how the query layer of PostgreSQL is reused, specifically its components that receive the query (*postman*), the query *parser*, *rewriter*, *analyzer*, as well as components responsible for *planning* and *executing* the query. Some of these components have been modified to perform efficiently in a distributed SQL database.

![Reusing the PostgreSQL query layer in YSQL](/images/section_icons/architecture/Reusing-PostgreSQL-query-layer.png)

## PostgreSQL Features in YSQL

The following table lists the most important YSQL features which you would find familiar if you have worked with PostgreSQL.

| YSQL Feature                                                 | Description                                                  |
| ------------------------------------------------------------ | ------------------------------------------------------------ |
| <span style="font-size:16px">[Basics](databases-schemas-tables/)</span> | SQL shell with `ysqlsh`, users, databases, tables and schemas |
| <span style="font-size:16px">[Data types](data-types/)</span> | String, numeric, temporal types, `SERIAL` pseudo type, `ENUM`, arrays, composite types |
| <span style="font-size:16px">[Data Manipulation](data-manipulation/)</span> | `INSERT`, `UPDATE`, `DELETE`, `INSERT ... ON CONFLICT`, and `RETURNING` clauses |
| <span style="font-size:16px">[Queries and Joins](queries/)</span> | Queries, joins, `FROM`, `GROUP BY`, `HAVING` clauses, common table expressions, recursive queries |
| <span style="font-size:16px">[Triggers](triggers/)</span>    | Triggers (on data modification) and event triggers (on schema changes) |

<!--
| <span style="font-size:16px">[Functions and operators](functions-operators/)</span> | Conditional expressions, math / string / date / time / window functions and operators  |
| <span style="font-size:16px">[Stored Procedures](stored-procedures/)</span> | Support for the various stored procedures |
| <span style="font-size:16px">[Triggers](triggers/)</span>                   | Triggers (on data modification) and event triggers (on schema changes) |
| <span style="font-size:16px">[Table Partitions](table-partitions)</span>    | List, range and hash partitioning of tables               |
| <span style="font-size:16px">[Advanced Topics](advanced-topics/)</span>     | Using `VIEWS`, PostgreSQL extensions supported in YSQL, temporary tables, etc. |
-->
See also:

* [Document data types (`JSONB` and `JSON`)](../json-support/jsonb-ysql)
* [Distributed transactions](../transactions)

<!--
* [Indexes and constraints]()
-->

## What's Extra in YSQL?

Since YugabyteDB is a distributed SQL database, there is a number of features that are availale in YSQL yet not present in PostgreSQL, as summarized in the following table.

| YSQL Feature                                                 | Description                                                  |
| ------------------------------------------------------------ | ------------------------------------------------------------ |
| <span style="font-size:16px">Data distribution with `HASH`</span> | Enables the use of `HASH` sort order, in addition to `ASC` and `DESC` for indexes |
| <span style="font-size:16px">`TABLESPACES` for geographic placement</span> | Enables pinning of data in tables and table partitions to different geographic locations |
| <span style="font-size:16px">`TABLEGROUPS` for colocating tables</span> | Enables colocation of multiple smaller tables into one tablet for better performance |

<!--
Read more about these [YSQL features not present in PostgreSQL](ysql-features-not-in-postgres/).
-->
