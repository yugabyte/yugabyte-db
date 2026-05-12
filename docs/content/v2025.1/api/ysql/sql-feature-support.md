---
title: SQL feature support
linkTitle: SQL compatibility
description: Summary of YugabyteDB's conformance to the SQL standard
headcontent: YugabyteDB supports most standard SQL features
menu:
  v2025.1_api:
    identifier: explore-ysql-language-feature-support
    parent: api-ysql
    weight: 120
type: docs
---

YugabyteDB is a distributed SQL database that implements many [standard SQL](https://en.wikipedia.org/wiki/SQL) features while introducing some unique capabilities due to its distributed nature. The following provides an overview of SQL features that are fully supported, partially supported, and features that are currently work in progress. Whether you're designing new applications or migrating existing workloads, this guide will help you understand how YugabyteDB's SQL capabilities compare to other SQL-based systems, ensuring smooth adoption and development.

## Data types

|                |                                 Data type                                 |                                       Documentation                                        |
| :------------: | :------------------------------------------------------------------------ | :----------------------------------------------------------------------------------------- |
| {{<icon/yes>}} | `ARRAY`                                                                   | [Array data types](../datatypes/type_array/)                                |
| {{<icon/yes>}} | `BINARY`                                                                  | [Binary data types](../datatypes/type_binary/)                              |
| {{<icon/yes>}} | `BIT`,`BYTES`                                                             |                                                                                            |
| {{<icon/yes>}} | `BOOLEAN`                                                                 | [Boolean data types](../datatypes/type_bool/)                               |
| {{<icon/yes>}} | `CHAR`, `VARCHAR`, `TEXT`                                                 | [Character data types](../datatypes/type_character/)                        |
| {{<icon/yes>}} | `COLLATE`                                                                 | [Collations](../../../explore/ysql-language-features/advanced-features/collations/)                   |
| {{<icon/yes>}} | `DATE`, `TIME`, `TIMESTAMP`, `INTERVAL`                                   | [Date and time data types](../datatypes/type_datetime/)                     |
| {{<icon/yes>}} | `DEC`, `DECIMAL`, `NUMERIC`                                               | [Fixed point numbers](../datatypes/type_numeric/#fixed-point-numbers)       |
| {{<icon/yes>}} | `ENUM`                                                                    | [Enumerations](../../../explore/ysql-language-features/data-types/#enumerations-enum-type)            |
| {{<icon/yes>}} | `FLOAT`, `REAL`, `DOUBLE PRECISION`                                       | [Floating-point numbers](../datatypes/type_numeric/#floating-point-numbers) |
| {{<icon/yes>}} | `JSON`, `JSONB`                                                           | [JSON data types](../datatypes/type_json/)                                  |
| {{<icon/yes>}} | `MONEY`                                                                   | [Money data types](../datatypes/type_money/)                                |
| {{<icon/yes>}} | `SERIAL`, `SMALLSERIAL`, `BIGSERIAL`                                      | [Serial data types](../datatypes/type_serial/)                              |
| {{<icon/yes>}} | `SMALLINT, INT, INTEGER, BIGINT`                                          | [Integers](../datatypes/type_numeric/#integers)                             |
| {{<icon/yes>}} | `INT4RANGE`, `INT8RANGE`, `NUMRANGE`, `TSRANGE`, `TSTZRANGE`, `DATERANGE` | [Range data types](../datatypes/type_range/)                                |
| {{<icon/yes>}} | `UUID`                                                                    | [UUID data type](../datatypes/type_uuid/)                                   |
| {{<icon/no>}}  | `XML`                                                                     |                                                                                            |
| {{<icon/yes>}} | `TSVECTOR`                                                                |                                                                                            |
| {{<icon/yes>}} | UDT(Base, Enumerated, Range, Composite, Array, Domain types)              |                                                                                            |
{.sno-1}

## Schema operations

|                    |             Operation             |                                                            Documentation                                                             |
| :----------------: | :-------------------------------- | :----------------------------------------------------------------------------------------------------------------------------------- |
| {{<icon/partial>}} | Altering tables                   | [ALTER TABLE](../the-sql-language/statements/ddl_alter_table/)                                                        |
|   {{<icon/yes>}}   | Altering databases                | [ALTER DATABASE](../the-sql-language/statements/ddl_alter_db/)                                                        |
|   {{<icon/yes>}}   | Altering a column's name          |                                                                                                                                      |
|   {{<icon/yes>}}   | Altering a column's default value |                                                                                                                                      |
| {{<icon/partial>}} | Altering a column's data type     |                                                                                                                                      |
|   {{<icon/yes>}}   | Adding columns                    | [ADD COLUMN](../the-sql-language/statements/ddl_alter_table/)                                                         |
|   {{<icon/yes>}}   | Removing columns                  | [DROP COLUMN](../the-sql-language/statements/ddl_alter_table/)                                                        |
|   {{<icon/yes>}}   | Adding constraints                | [ADD CONSTRAINT](../the-sql-language/statements/ddl_alter_table/#add-alter-table-constraint-constraints)              |
|   {{<icon/yes>}}   | Removing constraints              | [DROP CONSTRAINT](../the-sql-language/statements/ddl_alter_table/#drop-constraint-constraint-name-restrict-cascade)   |
|   {{<icon/no>}}    | Altering indexes                  |                                                                                                                                      |
|   {{<icon/yes>}}   | Adding indexes                    | [CREATE INDEX](../the-sql-language/statements/ddl_create_index/)                                                      |
|   {{<icon/yes>}}   | Removing indexes                  |                                                                                                                                      |
|   {{<icon/yes>}}   | Adding a primary key              |                                                                                                                                      |
|   {{<icon/yes>}}   | Dropping a primary key            |                                                                                                                                      |
|   {{<icon/no>}}    | Altering a primary key            |                                                                                                                                      |
|   {{<icon/yes>}}   | Adding user-defined schemas       | [CREATE SCHEMA](../the-sql-language/statements/ddl_create_schema/)                                                    |
|   {{<icon/no>}}    | Removing user-defined schemas     |                                                                                                                                      |
|   {{<icon/no>}}    | Altering user-defined schemas     |                                                                                                                                      |
{.sno-1}

## Constraints

|                    |                    Feature                    |                                      Documentation                                      |
| :----------------: | :-------------------------------------------- | :-------------------------------------------------------------------------------------- |
|   {{<icon/yes>}}   | Check                                         | [Check constraint](../../../explore/ysql-language-features/data-manipulation/#check-constraint)       |
|   {{<icon/yes>}}   | Unique                                        | [Unique constraint](../../../explore/ysql-language-features/data-manipulation/#unique-constraint)     |
|   {{<icon/yes>}}   | Not Null                                      | [Not Null constraint](../../../explore/ysql-language-features/data-manipulation/#not-null-constraint) |
|   {{<icon/yes>}}   | Primary Key                                   | [Primary keys](../../../explore/ysql-language-features/indexes-constraints/primary-key-ysql/)                             |
|   {{<icon/yes>}}   | Foreign Key                                   | [Foreign keys](../../../explore/ysql-language-features/data-manipulation/#foreign-key-constraint/)                             |
| {{<icon/partial>}} | Default Value                                 |                                                                                         |
| {{<icon/partial>}} | Deferrable Foreign Key constraints            |                                                                                         |
|   {{<icon/no>}}    | Deferrable Primary Key and Unique constraints |                                                                                         |
|   {{<icon/no>}}    | Exclusion constraints                         |                                                                                         |
{.sno-1}

## Indexes

|                |      Component       |                             Documentation                              |
| :------------: | :------------------- | :--------------------------------------------------------------------- |
| {{<icon/yes>}} | Indexes              | [Indexes and constraints](../../../explore/ysql-language-features/indexes-constraints/)                  |
| {{<icon/yes>}} | GIN indexes          | [GIN indexes](../../../explore/ysql-language-features/indexes-constraints/gin/)                          |
| {{<icon/yes>}} | Partial indexes      | [Partial indexes](../../../explore/ysql-language-features/indexes-constraints/partial-index-ysql/)       |
| {{<icon/yes>}} | Expression indexes   | [Expression indexes](../../../explore/ysql-language-features/indexes-constraints/expression-index-ysql/) |
| {{<icon/yes>}} | Multi-column indexes | [Multi-column indexes](../../../explore/ysql-language-features/indexes-constraints/secondary-indexes-ysql/#multi-column-index) |
| {{<icon/yes>}} | Covering indexes     | [Covering indexes](../../../explore/ysql-language-features/indexes-constraints/covering-index-ysql/)     |
| {{<icon/no>}}  | GiST indexes         |                                                                        |
| {{<icon/no>}}  | BRIN indexes         |                                                                        |
| {{<icon/yes>}} | B-tree indexes       | B-tree index is treated as an LSM index.                               |
{.sno-1}

## Transactions

|                |          Feature           |                                       Documentation                                        |
| :------------: | :------------------------- | :----------------------------------------------------------------------------------------- |
| {{<icon/yes>}} | Transactions               | [Transactions](../../../explore/transactions/)                                                        |
| {{<icon/yes>}} | `BEGIN`                    | [BEGIN](../the-sql-language/statements/txn_begin/)                          |
| {{<icon/yes>}} | `COMMIT`                   | [COMMIT](../the-sql-language/statements/txn_commit/)                        |
| {{<icon/yes>}} | `ROLLBACK`                 | [ROLLBACK](../the-sql-language/statements/txn_rollback/)                    |
| {{<icon/yes>}} | `SAVEPOINT`                | [SAVEPOINT](../the-sql-language/statements/savepoint_create/)               |
| {{<icon/yes>}} | `ROLLBACK TO SAVEPOINT`    | [ROLLBACK TO SAVEPOINT](../the-sql-language/statements/savepoint_rollback/) |
| {{<icon/no>}}  | `PREPARE TRANSACTION (XA)` |                                                                                            |
{.sno-1}

## Roles and Permissions

|                |       Component       |                                  Details                                  |
| :------------: | :-------------------- | :------------------------------------------------------------------------ |
| {{<icon/yes>}} | Users                 | [Manage users and roles](../../../secure/authorization/create-roles/)     |
| {{<icon/yes>}} | Roles                 | [Manage users and roles](../../../secure/authorization/create-roles/)     |
| {{<icon/yes>}} | Object ownership      |                                                                           |
| {{<icon/yes>}} | Privileges            | [Grant privileges](../../../secure/authorization/ysql-grant-permissions/) |
| {{<icon/yes>}} | Default privileges    |                                                                           |
| {{<icon/yes>}} | Row level security    |                                                                           |
| {{<icon/yes>}} | Column level security |                                                                           |
{.sno-1}

## Queries

|                |                              Component                              |                                  Details                                   |
| :------------: | :------------------------------------------------------------------ | :------------------------------------------------------------------------- |
| {{<icon/yes>}} | FROM, WHERE, GROUP BY, HAVING, DISTINCT, LIMIT/OFFSET, WITH queries | [Group data](../../../explore/ysql-language-features/queries/#group-data)                                       |
| {{<icon/yes>}} | EXPLAIN query plans                                                 | [Analyze queries with EXPLAIN](../../../launch-and-manage/monitor-and-alert/query-tuning/explain-analyze/) |
| {{<icon/yes>}} | JOINs (INNER/OUTER, LEFT/RIGHT)                                     | [Join columns](../../../explore/ysql-language-features/queries/#join-columns)                                   |
| {{<icon/yes>}} | Expressions and Operators                                           | [Expressions and operators](../../../explore/ysql-language-features/expressions-operators/)                     |
| {{<icon/yes>}} | Common Table Expressions (CTE) and Recursive Queries                | [Recursive queries and CTEs](../../../explore/ysql-language-features/queries/#ctes)       |
| {{<icon/yes>}} | Upserts (INSERT ... ON CONFLICT DO NOTHING/UPDATE)                  | [Upsert](../../../explore/ysql-language-features/data-manipulation/#upsert)                                     |
{.sno-1}

## Advanced SQL

|                |                      Component                       |                                      Details                                       |
| :------------: | :--------------------------------------------------- | :--------------------------------------------------------------------------------- |
| {{<icon/yes>}} | Stored procedures                                    | [Stored procedures](../../../explore/ysql-language-features/advanced-features/stored-procedures/)                                         |
| {{<icon/yes>}} | User-defined functions                               | [Functions](../user-defined-subprograms-and-anon-blocks/#functions) |
| {{<icon/yes>}} | Cursors                                              | [Cursors](../../../explore/ysql-language-features/advanced-features/cursor/)                                            |
| {{<icon/yes>}} | Row-level triggers (BEFORE, AFTER, INSTEAD OF)       |                                                                                    |
| {{<icon/yes>}} | Statement-level triggers (BEFORE, AFTER, INSTEAD OF) |                                                                                    |
| {{<icon/no>}}  | Deferrable triggers                                  |                                                                                    |
| {{<icon/no>}}  | Transition tables (REFERENCING clause for triggers)  |                                                                                    |
| {{<icon/yes>}} | Sequences                                            | [Auto-Increment column values](../../../explore/ysql-language-features/data-manipulation/#auto-increment-column-values) |
| {{<icon/yes>}} | Identity columns                                     |                                                                                    |
| {{<icon/yes>}} | Views                                                | [Views](../../../explore/ysql-language-features/advanced-features/views/)                                               |
| {{<icon/yes>}} | Materialized views                                   | [Materialized views](../../../explore/ysql-language-features/advanced-features/views/#materialized-views)               |
| {{<icon/yes>}} | Window functions                                     | [Window functions](../exprs/window_functions/)                      |
| {{<icon/yes>}} | Common table expressions                             |                                                                                    |
| {{<icon/yes>}} | Extensions                                           | [PostgreSQL extensions](../../../additional-features/pg-extensions/)                                         |
| {{<icon/yes>}} | Foreign data wrappers                                | [Foreign data wrappers](../../../explore/ysql-language-features/advanced-features/foreign-data-wrappers/)               |
{.sno-1}
