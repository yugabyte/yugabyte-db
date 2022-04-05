---
title: SQL Feature Support
linkTitle: SQL Feature Support
description: Summary of YugabyteDB's conformance to the SQL standard
image: /images/section_icons/secure/create-roles.png
menu:
  latest:
    identifier: explore-ysql-language-feature-support
    parent: explore-ysql-language-features
    weight: 50
isTocNested: true
showAsideToc: true
---
To understand which standard SQL features we support, refer the following tables:

### Data types

 Data type | Supported | Documentation|
-----------|-----------|---------
 `ARRAY` | ✓ | [Array documentation]/preview/api/ysql/datatypes/type_array/)|
 `BINARY` | ✓ | [Binary documentation]/preview/api/ysql/datatypes/type_binary/)|
 `BIT`,`BYTES` | ✓ |  |
 `BOOLEAN` | ✓ | [Boolean documentation]/preview/api/ysql/datatypes/type_bool/)|
 `CHAR`, `VARCHAR`, `TEXT` | ✓ | [Character data types documentation]/preview/api/ysql/datatypes/type_character/)|
 `COLLATE` | ✓ | [Collate documentation]/preview/explore/ysql-language-features/advanced-features/collations/#root)|
 `DATE`, `TIME`, `TIMESTAMP`, `INTERVAL` | ✓ | [Date and time data types documentation]/preview/api/ysql/datatypes/type_datetime/)|
 `DEC`, `DECIMAL`, `NUMERIC` | ✓ | [ Fixed point numbers documentation]/preview/api/ysql/datatypes/type_numeric/#fixed-point-numbers)|
 `ENUM` | ✓ |[ENUM documentation]/preview/explore/ysql-language-features/data-types/#enumerations-enum-type)|
 `FLOAT`, `REAL`, `DOUBLE PRECISION` | ✓ | [Floating point numbers documentation]/preview/api/ysql/datatypes/type_numeric/)|
 `JSON`, `JSONB` | ✓ | [JSON data types documentation]/preview/api/ysql/datatypes/type_json/)|
 `MONEY` | ✓ | [Money data type documentation]/preview/api/ysql/datatypes/type_money/)|
 `SERIAL`, `SMALLSERIAL`, `BIGSERIAL`| ✓ | [Serial documentation]/preview/api/ysql/datatypes/type_serial/)|
 `SET`| ✗ | |
 `SMALLINT, INT, INTEGER, BIGINT` | ✓ | [Integers documentation]/preview/api/ysql/datatypes/type_numeric/)|
 `INT4RANGE`, `INT8RANGE`, `NUMRANGE`, `TSRANGE`, `TSTZRANGE`, `DATERANGE` | ✓ | [Range data types documentation]/preview/api/ysql/datatypes/type_range/)|
 `UUID` | ✓ | [UUID documentation]/preview/api/ysql/datatypes/type_uuid/)|
 `XML`| ✗ | |
 `TSVECTOR` | ✓ ||
 UDT(Base, Enumerated, Range, Composite, Array, Domain types) | ✓ ||

### Schema operations

 Operation | Supported | Documentation|
-----------|-----------|---------
Altering tables | ✓ | [`ALTER TABLE`  documentation]/preview/api/ysql/the-sql-language/statements/ddl_alter_table/)|
 Altering databases | ✓ | [`ALTER DATABASE` documentation]/preview/api/ysql/the-sql-language/statements/ddl_alter_db/)|
 Altering columns | ✗ | |
 Altering a column's data type | ✗ | |
 Adding columns | ✓ | [`ADD COLUMN` documentation]/preview/api/ysql/the-sql-language/statements/ddl_alter_table/#add-column-column-name-data-type-constraint-constraints)|
 Removing columns | ✓ | [`DROP COLUMN` documentation]/preview/api/ysql/the-sql-language/statements/ddl_alter_table/#drop-column-column-name-restrict-cascade)|
 Adding constraints | ✓ | [`ADD CONSTRAINT` documentation]/preview/api/ysql/the-sql-language/statements/ddl_alter_table/#add-alter-table-constraint-constraints)|
 Removing constraints | ✓ | [`DROP CONSTRAINT` documentation]/preview/api/ysql/the-sql-language/statements/ddl_alter_table/#drop-constraint-constraint-name-restrict-cascade)|
 Altering indexes | ✗ | |
 Adding indexes | ✓ | [`CREATE INDEX` documentation]/preview/api/ysql/the-sql-language/statements/ddl_create_index/)|
 Removing indexes | ✗ | |
 Altering a primary key | ✗ | |
 Adding user-defined schemas | ✓ |  [`CREATE SCHEMA` documentation]/preview/api/ysql/the-sql-language/statements/ddl_create_schema/)|
 Removing user-defined schemas | ✗ | |
 Altering user-defined schemas | ✗ ||

### Constraints

 Feature | Supported |  Documentation|
-----------|-----------|---------
 Check | ✓ | [Check documentation]/preview/explore/indexes-constraints/other-constraints/#check-constraint)|
 Unique | ✓ | [Unique documentation]/preview/explore/indexes-constraints/other-constraints/#unique-constraint)|
 Not Null | ✓ | [Not Null documentation]/preview/explore/indexes-constraints/other-constraints/#not-null-constraint)|
 Primary Key | ✓ | [Primary Key documentation]/preview/explore/indexes-constraints/primary-key-ysql/)|
 Foreign Key | ✓ | [Foreign Key documentation]/preview/explore/indexes-constraints/foreign-key-ysql/)|
 Default Value | ✗ | |
 Deferrable Foreign Key constraints | ✓ ||
 Deferrable Primary Key and Unique constraints | ✗ | |
 Exclusion constraints| ✗ | |

### Indexes

 Component | Supported |  Documentation|
-----------|-----------|---------
 Indexes | ✓ | [Indexes documentation]/preview/explore/indexes-constraints/)|
 GIN indexes | ✓ | [GIN Indexes documentation]/preview/explore/indexes-constraints/gin/)|
 Partial indexes | ✓ | [Partial indexes documentation]/preview/explore/indexes-constraints/partial-index-ysql/)|
 Expression indexes | ✓ | [Expression indexes]/preview/explore/indexes-constraints/expression-index-ysql/)|
 Multi-column indexes | ✓  ||
 Covering indexes | ✓  | |
 Prefix indexes | ✗ | Implement using [Expression indexes]/preview/explore/indexes-constraints/expression-index-ysql/)|
 Spatial indexes | ✗  ||
 Multiple indexes per query | ✗ ||
 Full-text indexes | ✗ | |
 GiST indexes | ✗ | |
 BRIN indexes | ✗ | |

### Transactions

 Feature | Supported | Documentation|
-----------|-----------|---------
 Transactions | ✓ | [Transactions documentation]/preview/explore/transactions/)|
 `BEGIN` | ✓ | [`BEGIN` documentation]/preview/api/ysql/the-sql-language/statements/txn_begin/)|
 `COMMIT` | ✓ | [`COMMIT` documentation]/preview/api/ysql/the-sql-language/statements/txn_commit/)|
 `ROLLBACK` | ✓ | [`ROLLBACK` documentation]/preview/api/ysql/the-sql-language/statements/txn_rollback/)|
 `SAVEPOINT` | ✓ |  [`SAVEPOINT` documentation]/preview/api/ysql/the-sql-language/statements/savepoint_create/)|
 `ROLLBACK TO SAVEPOINT` | ✓ |  [`ROLLBACK TO SAVEPOINT` documentation]/preview/api/ysql/the-sql-language/statements/savepoint_create/)|

### Roles and Permissions

 Component | Supported | Details|
-----------|-----------|---------
 Users | ✓ | |
 Roles | ✓ | |
 Object ownership | ✓ | |
 Privileges | ✓ | |
 Default privileges | ✗ ||

### Queries

 Component | Supported | Details|
-----------|-----------|---------
 FROM, WHERE, GROUP BY, HAVING, DISTINCT, LIMIT/OFFSET, WITH queries| ✓ | |
 EXPLAIN query plans| ✓ | |
 JOINs (INNER/OUTER, LEFT/RIGHT)| ✓ | |
 Expressions and Operators| ✓ | |
 Common Table Expressions (CTE) and Recursive Queries| ✓ | |
 Upserts (INSERT ... ON CONFLICT DO NOTHING/UPDATE)| ✓ | |

### Advanced SQL

 Component | Supported | Details|
-----------|-----------|---------
 Stored procedures | ✓ | |
 User-defined functions| ✓ | |
 Cursors | ✓ | |
 Row-level triggers (BEFORE, AFTER, INSTEAD OF)| ✓ | |
 Statement-level triggers (BEFORE, AFTER, INSTEAD OF)| ✓ | |
 Deferrable triggers | ✗ ||
 Transition tables (REFERENCING clause for triggers) | ✗ ||
 Sequences |  ✓ | |
 Identity columns | ✓ ||
 Views | ✓ | |
 Materialized views | ✓ | |
 Window functions | ✓ | |
 Common table expressions | ✓||
 Extensions| ✓||
 Foreign data wrappers| ✓||
