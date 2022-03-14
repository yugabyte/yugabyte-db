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

### Schema operations

 Operation | Supported | Documentation
-----------|-----------|---------
Altering tables | ✓ | [`ALTER TABLE`  documentation](https://docs.yugabyte.com/latest/api/ysql/the-sql-language/statements/ddl_alter_table/)
 Altering databases | ✓ | [`ALTER DATABASE` documentation](https://docs.yugabyte.com/latest/api/ysql/the-sql-language/statements/ddl_alter_db/)
 Altering columns | ✗ | 
 Altering a column's data type | ✗ | 
 Adding columns | ✓ | [`ADD COLUMN` documentation](https://docs.yugabyte.com/latest/api/ysql/the-sql-language/statements/ddl_alter_table/#add-column-column-name-data-type-constraint-constraints)
 Removing columns | ✓ | [`DROP COLUMN` documentation](https://docs.yugabyte.com/latest/api/ysql/the-sql-language/statements/ddl_alter_table/#drop-column-column-name-restrict-cascade)
 Adding constraints | ✓ | [`ADD CONSTRAINT` documentation](https://docs.yugabyte.com/latest/api/ysql/the-sql-language/statements/ddl_alter_table/#add-alter-table-constraint-constraints)
 Removing constraints | ✓ | [`DROP CONSTRAINT` documentation](https://docs.yugabyte.com/latest/api/ysql/the-sql-language/statements/ddl_alter_table/#drop-constraint-constraint-name-restrict-cascade)
 Altering indexes | ✗ | 
 Adding indexes | ✓ | [`CREATE INDEX` documentation](https://docs.yugabyte.com/latest/api/ysql/the-sql-language/statements/ddl_create_index/)
 Removing indexes | ✗ | 
 Altering a primary key | ✗ | 
 Adding user-defined schemas | ✓ |  [`CREATE SCHEMA` documentation](https://docs.yugabyte.com/latest/api/ysql/the-sql-language/statements/ddl_create_schema/)
 Removing user-defined schemas | ✗ | 
 Altering user-defined schemas | ✗ |
 
### Data types

 Data type | Supported | Documentation
-----------|-----------|---------
 `ARRAY` | ✓ | [Array documentation](https://docs.yugabyte.com/latest/api/ysql/datatypes/type_array/)
 `BINARY` | ✓ | [Binary documentation](https://docs.yugabyte.com/latest/api/ysql/datatypes/type_binary/)
 `BIT` | ✓ |  
 `BYTES` | ✓ | 
 `BOOLEAN` | ✓ | [Boolean documentation](https://docs.yugabyte.com/latest/api/ysql/datatypes/type_bool/)
 `CHAR`, `VARCHAR`, `TEXT` | ✓ | [Character data types documentation](https://docs.yugabyte.com/latest/api/ysql/datatypes/type_character/)
 `COLLATE` | ✓ | [Collate documentation](https://docs.yugabyte.com/latest/explore/ysql-language-features/advanced-features/collations/#root)
 `DATE`, `TIME`, `TIMESTAMP`, `INTERVAL` | ✓ | [Date and time data types documentation](https://docs.yugabyte.com/latest/api/ysql/datatypes/type_datetime/)
 `DEC`, `DECIMAL`, `NUMERIC` | ✓ | [ Fixed point numbers documentation](https://docs.yugabyte.com/latest/api/ysql/datatypes/type_numeric/#fixed-point-numbers)
 `ENUM` | ✓ |[ENUM documentation](https://docs.yugabyte.com/latest/explore/ysql-language-features/data-types/#enumerations-enum-type)
 `FLOAT`, `REAL`, `DOUBLE PRECISION` | ✓ | [Floating point numbers documentation](https://docs.yugabyte.com/latest/api/ysql/datatypes/type_numeric/)
 `JSON`, `JSONB` | ✓ | [JSON data types documentation](https://docs.yugabyte.com/latest/api/ysql/datatypes/type_json/)
 `MONEY` | ✓ | [Money data type documentation](https://docs.yugabyte.com/latest/api/ysql/datatypes/type_money/)
 `SERIAL`, `SMALLSERIAL`, `BIGSERIAL`| ✓ | [Serial documentation](https://docs.yugabyte.com/latest/api/ysql/datatypes/type_serial/)
 `SET`| ✗ | 
 `SMALLINT, INT, INTEGER, BIGINT` | ✓ | [Integers documentation](https://docs.yugabyte.com/latest/api/ysql/datatypes/type_numeric/)
 `INT4RANGE`, `INT8RANGE`, `NUMRANGE`, `TSRANGE`, `TSTZRANGE`, `DATERANGE` | ✓ | [Range data types documentation](https://docs.yugabyte.com/latest/api/ysql/datatypes/type_range/)
 `UUID` | ✓ | [UUID documentation](https://docs.yugabyte.com/latest/api/ysql/datatypes/type_uuid/)
 `XML`| ✗ | 
 `TSVECTOR` | ✓ |
 UDT | ✓ |
 
### Indexes

 Component | Supported |  Documentation
-----------|-----------|---------
 Indexes | ✓ | [Indexes documentation](https://docs.yugabyte.com/latest/explore/indexes-constraints/overview/)
 GIN indexes | ✓ | [GIN Indexes documentation](https://docs.yugabyte.com/latest/explore/indexes-constraints/gin/)
 Partial indexes | ✓ | [Partial indexes documentation](https://docs.yugabyte.com/latest/explore/indexes-constraints/partial-index-ysql/)
 Expression indexes | ✓ | [Expression indexes](https://docs.yugabyte.com/latest/explore/indexes-constraints/expression-index-ysql/)
 Multi-column indexes | ✗  |
 Covering indexes | ✗  | 
 Spatial indexes | ✗  |
 Multiple indexes per query | ✗ |
 Full-text indexes | ✗ | 
 Prefix indexes | ✗ | Implement using [Expression indexes](https://docs.yugabyte.com/latest/explore/indexes-constraints/expression-index-ysql/)
 Hash indexes | ✓ | 
 GiST indexes | ✗ | 
 BRIN indexes | ✗ | 
 
### Constraints

 Feature | Supported |  Documentation
-----------|-----------|---------
 Check | ✓ | [Check documentation](https://docs.yugabyte.com/latest/explore/indexes-constraints/other-constraints/#check-constraint)
 Unique | ✓ | [Unique documentation](https://docs.yugabyte.com/latest/explore/indexes-constraints/other-constraints/#unique-constraint)
 Not Null | ✓ | [Not Null documentation](https://docs.yugabyte.com/latest/explore/indexes-constraints/other-constraints/#not-null-constraint)
 Primary Key | ✓ | [Primary Key documentation](https://docs.yugabyte.com/latest/explore/indexes-constraints/primary-key-ysql/)
 Foreign Key | ✓ | [Foreign Key documentation](https://docs.yugabyte.com/latest/explore/indexes-constraints/foreign-key-ysql/)
 Default Value | ✗ | 

### Transactions

 Feature | Supported | Documentation
-----------|-----------|---------
 Transactions | ✓ | [Transactions documentation](https://docs.yugabyte.com/latest/explore/transactions/)
 `BEGIN` | ✓ | [`BEGIN` documentation](https://docs.yugabyte.com/latest/api/ysql/the-sql-language/statements/txn_begin/)
 `COMMIT` | ✓ | [`COMMIT` documentation](https://docs.yugabyte.com/latest/api/ysql/the-sql-language/statements/txn_commit/)
 `ROLLBACK` | ✓ | [`ROLLBACK` documentation](https://docs.yugabyte.com/latest/api/ysql/the-sql-language/statements/txn_rollback/)
 `SAVEPOINT` | ✓ |  [`SAVEPOINT` documentation](https://docs.yugabyte.com/latest/api/ysql/the-sql-language/statements/savepoint_create/)
 `ROLLBACK TO SAVEPOINT` | ✓ |  [`ROLLBACK TO SAVEPOINT` documentation](https://docs.yugabyte.com/latest/api/ysql/the-sql-language/statements/savepoint_create/)

### Roles and Permissions

 Component | Supported | Documentation
-----------|-----------|---------
 Users | ✓ | 
 Roles | ✓ | 
 Object ownership | ✓ | 
 Privileges | ✓ | 
 Default privileges | ✗ |
 RLS/CLS | |
 
### Miscellaneous

 Component | Supported | Details
-----------|-----------|---------
 Stored procedures | ✓ | 
 Cursors | ✓ | 
 Triggers | ✓ | 
 Sequences |  ✓ | 
 Identity columns | ✓ |
 Views | ✓ | 
 Materialized views | ✓ | 
 Window functions | ✓ | 
 Common table expressions | ✓|
 
