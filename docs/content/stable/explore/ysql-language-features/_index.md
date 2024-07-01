---
title: SQL features
headerTitle: SQL features
linkTitle: SQL features
description: Explore core SQL features in YSQL
headcontent: Explore core SQL features in YSQL
image: /images/section_icons/api/ysql.png
menu:
  stable:
    identifier: explore-ysql-language-features
    parent: explore
    weight: 100
type: indexpage
showRightNav: true
---
YugabyteDB's [YSQL API](../../api/ysql/) reuses a fork of the query layer of PostgreSQL as its starting point and runs on top of YugabyteDB's distributed storage layer called DocDB. This architecture allows YSQL to support most PostgreSQL features, such as data types, queries, expressions, operators and functions, stored procedures, triggers, extensions, and so on, all of which are expected to work identically on both database systems.

{{< tip title="Tip" >}}
A large portion of the documentation and examples written for PostgreSQL would work against YSQL.

{{< /tip >}}

The following diagram shows how YugabyteDB reuses the PostgreSQL query layer, specifically the components that receive the query (_postman_), the query _parser_, _rewriter_, and _analyzer_, as well as components responsible for _planning_ and _executing_ the query. Some of these components have been modified to perform efficiently in a distributed SQL database.

![Reusing the PostgreSQL query layer in YSQL](/images/section_icons/architecture/Reusing-PostgreSQL-query-layer.png)

## SQL features in YSQL

The following table lists the most important YSQL features which you would find familiar if you have worked with PostgreSQL.

| YSQL Feature | Description |
| :----------- | :---------- |
| [Schemas and Tables](databases-schemas-tables/) | SQL shell with `ysqlsh`, users, databases, tables, and schemas |
| [Data Types](data-types/) | String, numeric, temporal types, `SERIAL` pseudo type, `ENUM`, arrays, composite types |
| [DDL Statements](../../api/ysql/the-sql-language/statements/#data-definition-language-ddl/) | Data definition language |
| [Data Manipulation](data-manipulation/) | `INSERT`, `UPDATE`, `DELETE`, `INSERT ... ON CONFLICT`, and `RETURNING` clauses |
| [Queries and Joins](queries/) | Queries, joins, `FROM`, `GROUP BY`, `HAVING` clauses, common table expressions, recursive queries |
| [Join strategies](join-strategies/) | Inner join, outer join, full outer join, cross join, natural join. |
| [Expressions and Operators](expressions-operators/) | Basic operators and boolean, numeric, date expressions |
| [Indexes](indexes-constraints/) | Support for indexes and constraints |
| [Stored Procedures](stored-procedures/) | Support for stored procedures |
| [Extensions](pg-extensions/) | Support for PostgreSQL extensions |

## Advanced features in YSQL

The following table lists the advanced features in YSQL.

| YSQL Feature | Description |
| :----------- | :---------- |
| [Cursors](advanced-features/cursor/) | Declaration of cursors in YSQL |
| [Table Partitioning](advanced-features/partitions/) | List, range, and hash partitioning of tables |
| [Views](advanced-features/views/) | Views and updatable views |
| [Savepoints](advanced-features/savepoints/) | Savepoints in YSQL |
| [Collations](advanced-features/collations/) | Collations in YSQL |
| [Foreign data wrappers](advanced-features/foreign-data-wrappers/) | Foreign data wrappers in YSQL |
| [Triggers](advanced-features/triggers/) | Triggers (on data modification) and event triggers (on schema changes) |
