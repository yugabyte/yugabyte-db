---
title: SQL features
headerTitle: SQL features
linkTitle: SQL features
description: Explore core SQL features in YSQL
headcontent: Explore core SQL features in YSQL
image: /images/section_icons/api/ysql.png
menu:
  v2.14:
    identifier: explore-ysql-language-features
    parent: explore
    weight: 200
type: indexpage
showRightNav: true
---
YugabyteDB's YSQL API reuses a fork of the query layer of PostgreSQL as its starting point and runs on top of YugabyteDB's distributed storage layer called DocDB. This architecture allows YSQL to support most PostgreSQL features, such as data types, queries, expressions, operators and functions, stored procedures, triggers, extensions, and so on, all of which are expected to work identically on both database systems.

{{< tip title="Tip" >}}
A large portion of the documentation and examples written for PostgreSQL would work against YSQL.

{{< /tip >}}

The following diagram shows how YugabyteDB reuses the PostgreSQL query layer, specifically the components that receive the query (_postman_), the query _parser_, _rewriter_, _analyzer_, as well as components responsible for _planning_ and _executing_ the query. Some of these components have been modified to perform efficiently in a distributed SQL database.

![Reusing the PostgreSQL query layer in YSQL](/images/section_icons/architecture/Reusing-PostgreSQL-query-layer.png)

## SQL Features in YSQL

The following table lists the most important YSQL features which you would find familiar if you have worked with PostgreSQL.

| YSQL Feature | Description |
| :----------- | :---------- |
| <span style="font-size:16px">[Schemas and Tables](databases-schemas-tables/)</span> | SQL shell with `ysqlsh`, users, databases, tables, and schemas |
| <span style="font-size:16px">[Data Types](data-types/)</span> | String, numeric, temporal types, `SERIAL` pseudo type, `ENUM`, arrays, composite types |
| <span style="font-size:16px">[DDL Statements](../../api/ysql/the-sql-language/statements/#data-definition-language-ddl/)</span> | Data definition language |
| <span style="font-size:16px">[Data Manipulation](data-manipulation/)</span> | `INSERT`, `UPDATE`, `DELETE`, `INSERT ... ON CONFLICT`, and `RETURNING` clauses |
| <span style="font-size:16px">[Queries and Joins](queries/)</span> | Queries, joins, `FROM`, `GROUP BY`, `HAVING` clauses, common table expressions, recursive queries |
| <span style="font-size:16px">[Expressions and Operators](expressions-operators/)</span> | Basic operators and boolean, numeric, date expressions |
| <span style="font-size:16px">[Stored Procedures](stored-procedures/)</span> | Support for stored procedures |
| <span style="font-size:16px">[Triggers](triggers/)</span> | Triggers (on data modification) and event triggers (on schema changes) |
| <span style="font-size:16px">[Extensions](pg-extensions/)</span> | Support for PostgreSQL extensions |

## Advanced features in YSQL

The following table lists the advanced features in YSQL.

| YSQL Feature | Description |
| :----------- | :---------- |
| <span style="font-size:16px">[Cursors](advanced-features/cursor/)</span> | Declaration of cursors in YSQL |
| <span style="font-size:16px">[Table Partitioning](advanced-features/partitions/)</span> | List, range, and hash partitioning of tables |
| <span style="font-size:16px">[Views](advanced-features/views/)</span> | Views and updatable views |
| <span style="font-size:16px">[Savepoints](advanced-features/savepoints/)</span> | Savepoints in YSQL |
| <span style="font-size:16px">[Collations](advanced-features/collations/)</span> | Collations in YSQL |

<!--
| <span style="font-size:16px">[Functions and operators](functions-operators/)</span> | Conditional expressions, math / string / date / time / window functions and operators  |
| <span style="font-size:16px">[Advanced Topics](advanced-topics/)</span>     | Using `VIEWS`, PostgreSQL extensions supported in YSQL, temporary tables, etc. |
-->

## Going beyond SQL

Because YugabyteDB is a distributed SQL database, YSQL has a number of features that are not present in PostgreSQL, as summarized in the following table.

| YSQL Feature                                                 | Description                                                  |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| <span style="font-size:16px">[Data distribution with `HASH`](../linear-scalability/sharding-data/)</span> | Enables the use of `HASH` sort order, in addition to `ASC` and `DESC` for indexes |
| <span style="font-size:16px">[`Tablespaces`](going-beyond-sql/tablespaces/)</span> | Enables pinning of data in tables and table partitions to different geographic locations |
| <span style="font-size:16px">[`Follower Reads`](going-beyond-sql/follower-reads-ysql/)</span> | Enables more read IOPS with low latencies in YugabyteDB clusters |
| <span style="font-size:16px">`Tablegroups`</span> | Enables colocation of multiple smaller tables into one tablet for better performance |
