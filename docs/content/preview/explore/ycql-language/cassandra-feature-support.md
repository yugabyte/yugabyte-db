---
title: Cassandra feature support
linkTitle: Cassandra feature support
description: Summary of YugabyteDB's parity to Cassandra features
headcontent: YugabyteDB supports most standard Cassandra features
image: /images/section_icons/secure/create-roles.png
menu:
  preview:
    identifier: explore-ycql-language-feature-support
    parent: explore-ycql-language
    weight: 50

rightNav:
  hideH3: true

type: docs
---

Yugabyte Cloud Query Language (YCQL) has its roots in the [Cassandra Query Language (CQL)](http://cassandra.apache.org/doc/latest/cql/index.html). This page highlights the important differences in feature support between YCQL and Cassandra 3.4.2.

## Data types

### Primitive Types

|                |   Category   |                                            Types                                             |
| :------------: | ------------ | :------------------------------------------------------------------------------------------- |
| {{<icon/yes>}} | Integers     | [BIGINT, COUNTER, INT, INTEGER, SMALLINT, TINYINT, VARINT](../../../api/ycql/type_int)      |
| {{<icon/yes>}} | Numbers      | [DECIMAL, DOUBLE, FLOAT](../../../api/ycql/type_number)                                      |
| {{<icon/yes>}} | Binary data  | [BLOB](../../../api/ycql/type_blob)                                                          |
| {{<icon/yes>}} | Boolean      | [BOOLEAN](../../../api/ycql/type_bool)                                                       |
| {{<icon/yes>}} | Date/Time    | [DATE, TIME, TIMESTAMP](../../../api/ycql/type_datetime/)                                    |
| {{<icon/yes>}} | Collections  | [FROZEN](../../../api/ycql/type_frozen), [LIST, MAP, SET](../../../api/ycql/type_collection) |
| {{<icon/yes>}} | IP Addresses | [INET](../../../api/ycql/type_inet)                                                          |
| {{<icon/yes>}} | Json         | [JSONB](../../../api/ycql/type_jsonb)                                                        |
| {{<icon/yes>}} | String       | [TEXT, VARCHAR](../../../api/ycql/type_text)                                                 |
| {{<icon/yes>}} | UUID         | [TIMEUUID, UUID](../../../api/ycql/type_uuid)                                                |
| {{<icon/no>}}  | TUPLE        |                                                                                              |
{.sno-1}

### User defined data types

|                |    Operation    |                     Details                      |
| :------------: | :-------------- | ------------------------------------------------ |
| {{<icon/yes>}} | Create new type | [CREATE TYPE](../../../api/ycql/ddl_create_type) |
| {{<icon/yes>}} | Delete types    | [DROP TYPE](../../../api/ycql/ddl_drop_type)   |
| {{<icon/no>}}  | Alter types     |                                                  |
{.sno-1}

## DDL

### Keyspaces

|                    |           Operation            |                             Details                             |
| :----------------: | :----------------------------- | :-------------------------------------------------------------- |
|   {{<icon/yes>}}   | Creating keyspace              | [CREATE KEYSPACE](../../../api/ycql/ddl_create_keyspace/)       |
|   {{<icon/yes>}}   | Check before creating keyspace | `CREATE KEYSPACE IF NOT EXISTS`                                 |
| {{<icon/partial>}} | Modifying keyspace             | [ALTER KEYSPACE](../../../api/ycql/ddl_alter_keyspace/) - No Op |
|   {{<icon/no>}}    | Check before altering keyspace | `ALTER KEYSPACE IF EXISTS`                                      |

{.sno-1}

### Tables

|                |          Operation           |                                    Details                                     |
| :------------: | :--------------------------- | :----------------------------------------------------------------------------- |
| {{<icon/yes>}} | Adding columns               | [ADD COLUMN](../../../api/ycql/ddl_alter_table#add-a-column-to-a-table)        |
| {{<icon/yes>}} | Altering tables              | [ALTER TABLE](../../../api/ycql/ddl_alter_table/)                              |
| {{<icon/yes>}} | Removing columns             | [DROP COLUMN](../../../api/ycql/ddl_alter_table/#remove-a-column-from-a-table) |
| {{<icon/yes>}} | Rename column's name         | [RENAME COLUMN](../../../api/ycql/ddl_alter_table/#rename-a-column-in-a-table) |
| {{<icon/no>}}  | Check before altering tables | `ALTER TABLE IF EXISTS`                                                                    |
{.sno-1}

### Indexes

|                |          Operation           |                                              Details                                              |
| :------------: | :--------------------------- | :------------------------------------------------------------------------------------------------ |
| {{<icon/yes>}} | Adding indexes               | [CREATE INDEX](../../../api/ycql/ddl_create_index/)                                               |
| {{<icon/yes>}} | Removing indexes             | [DROP INDEX](../../../api/ycql/ddl_drop_index/)                                                   |
| {{<icon/yes>}} | Partial indexes              | [Partial indexes](../../../api/ycql/ddl_create_index#partial-index)                               |
| {{<icon/yes>}} | Covering indexes             | [Covering indexes](../../../api/ycql/ddl_create_index#included-columns)                           |
| {{<icon/yes>}} | Unique indexes               | [Unique indexes](../../../api/ycql/ddl_create_index#unique-index)                                 |
| {{<icon/no>}}  | Adding indexes on Collection | Cannot create index on  `map/list/set/full jsonb/udt` and the keys,values,entries of a collection |
{.sno-1}

## DML

### Select

|                |                 Operation                 |                                 Details                                  |
| :------------: | :---------------------------------------- | :----------------------------------------------------------------------- |
| {{<icon/yes>}} | Select columns                            | [SELECT * FROM ...](../../../api/ycql/dml_select/)                       |
| {{<icon/yes>}} | Conditional select with `[NOT] IN` clause | [SELECT ... WHERE key IN ...](../../../api/ycql/dml_select#where-clause) |
| {{<icon/yes>}} | Conditional select with `IF` clause       | [SELECT ... IF ...](../../../api/ycql/dml_select#if-clause)              |
| {{<icon/yes>}}  | Select using `CONTAINS [KEY]`            | [SELECT * FROM ...](../../../api/ycql/dml_select/)                       |
| {{<icon/no>}}  | `SELECT JSON`                             | [JSONB](../../../api/ycql/type_jsonb/) is supported as a native type     |
| {{<icon/no>}}  | Select with `PER PARTITION LIMIT`         |                                                                          |
| {{<icon/no>}}  | Grouping results with `GROUP BY`          |                                                                          |
{.sno-1}

### Update

|                    |                 Operation                 |                              Details                              |
| :----------------: | :---------------------------------------- | :---------------------------------------------------------------- |
| {{<icon/partial>}} | Update columns                            | [UPDATE](../../../api/ycql/dml_update/) - Only single row updates |
| {{<icon/partial>}} | Conditional update with `[NOT] IN` clause | Only single row updates                                           |
|   {{<icon/yes>}}   | Conditional update with `IF` clause       | [UPDATE ... IF](../../../api/ycql/dml_update#if-clause)           |
|   {{<icon/yes>}}   | Update with `USING` clause                | [UPDATE ... USING](../../../api/ycql/dml_update#using-clause)     |
| {{<icon/no>}}      | Conditional Update using `CONTAINS [KEY]` | `UPDATE ... WHERE <col> CONTAINS ...`                             |
{.sno-1}

### Delete

|                |                 Operation                 |                                 Details                                  |
| :------------: | :---------------------------------------- | :----------------------------------------------------------------------- |
| {{<icon/yes>}} | Delete rows                               | [DELETE](../../../api/ycql/dml_delete/)                                  |
| {{<icon/yes>}} | Conditional delete with `IF` clause       | [DELETE ... IF](../../../api/ycql/dml_update#if-clause)                  |
| {{<icon/yes>}} | Delete with `USING` clause                | [DELETE ... USING](../../../api/ycql/dml_delete#using-clause)            |
| {{<icon/no>}}  | Conditional delete with `[NOT] IN` clause | [DELETE ... WHERE key IN ...](../../../api/ycql/dml_delete#where-clause) |
| {{<icon/no>}}  | Conditional delete using `CONTAINS [KEY]` | `DELETE ... WHERE <col> CONTAINS ...`                                    |
{.sno-1}

### Insert

|                |   Operation    |                                 Details                                  |
| :------------: | :------------- | :----------------------------------------------------------------------- |
| {{<icon/yes>}} | Adding columns | [INSERT ... INTO ...](../../../api/ycql/dml_insert/)                     |
| {{<icon/no>}}  | `INSERT JSON`  | [JSONB](../../../api/ycql/type_jsonb/) is supported as a native type |
{.sno-1}

## Transactions

|                |           Feature            |                              Details                              |
| :------------: | :--------------------------- | :---------------------------------------------------------------- |
| {{<icon/yes>}} | Begin a transaction          | [BEGIN TRANSACTION](../../../api/ycql/dml_transaction)            |
| {{<icon/yes>}} | End a transaction            | [END TRANSACTION](../../../api/ycql/dml_transaction)              |
| {{<icon/yes>}} | SQL style transaction start  | [START TRANSACTION](../../../api/ycql/dml_transaction#sql-syntax) |
| {{<icon/yes>}} | SQL style transaction commit | [COMMIT](../../../api/ycql/dml_transaction#sql-syntax)            |
{.sno-1}

## Security

|                |  Component   |                                    Details                                     |
| :------------: | :----------- | :----------------------------------------------------------------------------- |
| {{<icon/yes>}} | Roles        | [Manage users and roles](../../../secure/authorization/create-roles-ycql/)     |
| {{<icon/yes>}} | Permissions  | [Grant privileges](../../../secure/authorization/ycql-grant-permissions/)      |
| {{<icon/no>}}  | Users        | Legacy Cassandra feature (_CREATE, DROP, ALTER, LIST_)                          |
| {{<icon/no>}}  | `LIST ROLES` | But can be done using [query](../../../secure/authorization/create-roles-ycql) |
| {{<icon/no>}}  | `LIST PERMISSIONS` | But can be done using [query](../../../secure/authorization/ycql-grant-permissions/#2-list-permissions-for-roles) |
{.sno-1}

## Other Features

|                    |          Component           |                                    Details                                     |
| :----------------: | :--------------------------- | :----------------------------------------------------------------------------- |
|   {{<icon/yes>}}   | Aggregates                   | [AVG, COUNT, MAX, MIN, SUM](../../../api/ycql/expr_fcall/#aggregate-functions) |
|   {{<icon/yes>}}   | Built-in Functions           | [Now, DateOf, CurrentTime, ToTime, UUID ... ](../../../api/ycql/expr_fcall/)   |
|   {{<icon/yes>}}   | Operators                    | [Binary, Unary, Null operators](../../../api/ycql/expr_ocall/)                 |
| {{<icon/partial>}} | Batch                        | Only programmatically via [BatchStatement](../../../api/ycql/batch/)           |
|   {{<icon/no>}}    | Materialized Views           |                                                                                |
|   {{<icon/no>}}    | Triggers                     |                                                                                |
|   {{<icon/no>}}    | User Defined Aggregates(UDA) |                                                                                |
|   {{<icon/no>}}    | User-defined functions(UDF)  |                                                                                |
{.sno-1}

## Learn more

- [Comparison with Apache Cassandra](../../../faq/comparisons/cassandra)
- [YCQL command reference](../../../api/ycql/)