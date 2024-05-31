---
title: SQL feature support
linkTitle: SQL feature support
description: Summary of YugabyteDB's conformance to the SQL standard
headcontent: YugabyteDB supports most standard SQL features.
menu:
  v2.16:
    identifier: explore-ysql-language-feature-support
    parent: explore-ysql-language-features
    weight: 50
type: docs
---

To understand which standard SQL features we support, refer to the following tables:

### Data types

| Data type | Supported | Documentation |
| :-------- | :-------: | :------------ |
| `ARRAY` | ✓ | [Array data types](../../../api/ysql/datatypes/type_array/) |
| `BINARY` | ✓ | [Binary data types](../../../api/ysql/datatypes/type_binary/) |
| `BIT`,`BYTES` | ✓ | |
| `BOOLEAN` | ✓ | [Boolean data types](../../../api/ysql/datatypes/type_bool/) |
| `CHAR`, `VARCHAR`, `TEXT` | ✓ | [Character data types](../../../api/ysql/datatypes/type_character/) |
| `COLLATE` | ✓ | [Collations](../../ysql-language-features/advanced-features/collations/#root) |
| `DATE`, `TIME`, `TIMESTAMP`, `INTERVAL` | ✓ | [Date and time data types](../../../api/ysql/datatypes/type_datetime/) |
| `DEC`, `DECIMAL`, `NUMERIC` | ✓ | [Fixed point numbers](../../../api/ysql/datatypes/type_numeric/#fixed-point-numbers) |
| `ENUM` | ✓ |[Enumerations](../../ysql-language-features/data-types/#enumerations-enum-type) |
| `FLOAT`, `REAL`, `DOUBLE PRECISION` | ✓ | [Floating-point numbers](../../../api/ysql/datatypes/type_numeric/#floating-point-numbers) |
| `JSON`, `JSONB` | ✓ | [JSON data types](../../../api/ysql/datatypes/type_json/) |
| `MONEY` | ✓ | [Money data types](../../../api/ysql/datatypes/type_money/) |
| `SERIAL`, `SMALLSERIAL`, `BIGSERIAL`| ✓ | [Serial data types](../../../api/ysql/datatypes/type_serial/) |
| `SET`| ✗ | |
| `SMALLINT, INT, INTEGER, BIGINT` | ✓ | [Integers](../../../api/ysql/datatypes/type_numeric/#integers) |
| `INT4RANGE`, `INT8RANGE`, `NUMRANGE`, `TSRANGE`, `TSTZRANGE`, `DATERANGE` | ✓ | [Range data types](../../../api/ysql/datatypes/type_range/) |
| `UUID` | ✓ | [UUID data type](../../../api/ysql/datatypes/type_uuid/) |
| `XML`| ✗ | |
| `TSVECTOR` | ✓ | |
| UDT(Base, Enumerated, Range, Composite, Array, Domain types) | ✓ | |

### Schema operations

| Operation | Supported | Documentation |
| :-------- | :-------: | :------------ |
| Altering tables | ✓ | [ALTER TABLE](../../../api/ysql/the-sql-language/statements/ddl_alter_table/) |
| Altering databases | ✓ | [ALTER DATABASE](../../../api/ysql/the-sql-language/statements/ddl_alter_db/) |
| Altering columns | ✗ | |
| Altering a column's data type | ✗ | |
| Adding columns | ✓ | [ADD COLUMN](../../../api/ysql/the-sql-language/statements/ddl_alter_table/#add-column-column-name-data-type-constraint-constraints) |
| Removing columns | ✓ | [DROP COLUMN](../../../api/ysql/the-sql-language/statements/ddl_alter_table/#drop-column-column-name-restrict-cascade) |
| Adding constraints | ✓ | [ADD CONSTRAINT](../../../api/ysql/the-sql-language/statements/ddl_alter_table/#add-alter-table-constraint-constraints) |
| Removing constraints | ✓ | [DROP CONSTRAINT](../../../api/ysql/the-sql-language/statements/ddl_alter_table/#drop-constraint-constraint-name-restrict-cascade) |
| Altering indexes | ✗ | |
| Adding indexes | ✓ | [CREATE INDEX](../../../api/ysql/the-sql-language/statements/ddl_create_index/) |
| Removing indexes | ✗ | |
| Altering a primary key | ✗ | |
| Adding user-defined schemas | ✓ |  [CREATE SCHEMA](../../../api/ysql/the-sql-language/statements/ddl_create_schema/) |
| Removing user-defined schemas | ✗ | |
| Altering user-defined schemas | ✗ | |

### Constraints

| Feature | Supported | Documentation |
| :------ | :-------: | :------------ |
| Check | ✓ | [Check constraint](../../indexes-constraints/other-constraints/#check-constraint) |
| Unique | ✓ | [Unique constraint](../../indexes-constraints/other-constraints/#unique-constraint) |
| Not Null | ✓ | [Not Null constraint](../../indexes-constraints/other-constraints/#not-null-constraint) |
| Primary Key | ✓ | [Primary keys](../../indexes-constraints/primary-key-ysql/) |
| Foreign Key | ✓ | [Foreign keys](../../indexes-constraints/foreign-key-ysql/) |
| Default Value | ✗ | |
| Deferrable Foreign Key constraints | ✓ | |
| Deferrable Primary Key and Unique constraints | ✗ | |
| Exclusion constraints| ✗ | |

### Indexes

| Component | Supported | Documentation |
| :-------- | :-------: | :------------ |
| Indexes | ✓ | [Indexes and constraints](../../indexes-constraints/) |
| GIN indexes | ✓ | [GIN indexes](../../indexes-constraints/gin/) |
| Partial indexes | ✓ | [Partial indexes](../../indexes-constraints/partial-index-ysql/) |
| Expression indexes | ✓ | [Expression indexes](../../indexes-constraints/expression-index-ysql/) |
| Multi-column indexes | ✓  | |
| Covering indexes | ✓  | [Covering indexes](../../indexes-constraints/covering-index-ysql/) |
| Prefix indexes | ✗ | Implement using [Expression indexes](../../indexes-constraints/expression-index-ysql/) |
| Spatial indexes | ✗  | |
| Multiple indexes per query | ✗ | |
| Full-text indexes | ✗ | |
| GiST indexes | ✗ | |
| BRIN indexes | ✗ | |

### Transactions

| Feature | Supported | Documentation |
| :------ | :-------: | :------------ |
| Transactions | ✓ | [Transactions](../../transactions/) |
| `BEGIN` | ✓ | [BEGIN](../../../api/ysql/the-sql-language/statements/txn_begin/) |
| `COMMIT` | ✓ | [COMMIT](../../../api/ysql/the-sql-language/statements/txn_commit/) |
| `ROLLBACK` | ✓ | [ROLLBACK](../../../api/ysql/the-sql-language/statements/txn_rollback/) |
| `SAVEPOINT` | ✓ |  [SAVEPOINT](../../../api/ysql/the-sql-language/statements/savepoint_create/) |
| `ROLLBACK TO SAVEPOINT` | ✓ |  [ROLLBACK TO SAVEPOINT](../../../api/ysql/the-sql-language/statements/savepoint_rollback/) |

### Roles and Permissions

| Component | Supported | Details |
| :-------- | :-------: | :------ |
| Users | ✓ | [Manage users and roles](../../../secure/authorization/create-roles/) |
| Roles | ✓ | [Manage users and roles](../../../secure/authorization/create-roles/) |
| Object ownership | ✓ | |
| Privileges | ✓ | [Grant privileges](../../../secure/authorization/ysql-grant-permissions/) |
| Default privileges | ✗ | |

### Queries

| Component | Supported | Details |
| :-------- | :-------: | :------ |
| FROM, WHERE, GROUP BY, HAVING, DISTINCT, LIMIT/OFFSET, WITH queries| ✓ | [Group data](../queries/#group-data) |
| EXPLAIN query plans| ✓ | [Analyze queries with EXPLAIN](../../query-1-performance/explain-analyze/) |
| JOINs (INNER/OUTER, LEFT/RIGHT) | ✓ | [Join columns](../queries/#join-columns) |
| Expressions and Operators| ✓ | [Expressions and operators](../expressions-operators/) |
| Common Table Expressions (CTE) and Recursive Queries| ✓ | [Recursive queries and CTEs](../queries/#recursive-queries-and-ctes) |
| Upserts (INSERT ... ON CONFLICT DO NOTHING/UPDATE) | ✓ | [Upsert](../data-manipulation/#upsert) |

### Advanced SQL

| Component | Supported | Details |
| :-------- | :-------: | :------ |
| Stored procedures | ✓ | [Stored procedures](../stored-procedures/) |
| User-defined functions| ✓ | [Functions](../../../api/ysql/user-defined-subprograms-and-anon-blocks/#functions) |
| Cursors | ✓ | [Cursors](../advanced-features/cursor/) |
| Row-level triggers (BEFORE, AFTER, INSTEAD OF) | ✓ | |
| Statement-level triggers (BEFORE, AFTER, INSTEAD OF) | ✓ | |
| Deferrable triggers | ✗ | |
| Transition tables (REFERENCING clause for triggers) | ✗ | |
| Sequences |  ✓ | [Auto-Increment column values](../data-manipulation/#auto-increment-column-values) |
| Identity columns | ✓ | |
| Views | ✓ | [Views](../advanced-features/views/) |
| Materialized views | ✓ | [Materialized views](../advanced-features/views/#materialized-views)|
| Window functions | ✓ | [Window functions](../../../api/ysql/exprs/window_functions/)|
| Common table expressions | ✓| |
| Extensions| ✓| [PostgreSQL extensions](../pg-extensions/) |
| Foreign data wrappers| ✓| [Foreign data wrappers](../advanced-features/foreign-data-wrappers/) |
