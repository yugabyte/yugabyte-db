---
title: Cassandra feature support
linkTitle: Cassandra feature support
description: Summary of YugabyteDB's parity to Cassandra features
headcontent: YugabyteDB supports most standard Cassandra features.
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
|       |                     Primitive Type                     | Details |
| :------------: | :----------------------------------------------------- | ------- |
| {{<icon/yes>}} | [BIGINT](../../../api/ycql/type_int)                   |         |
| {{<icon/yes>}} | [BLOB](../../../api/ycql/type_blob)                    |         |
| {{<icon/yes>}} | [BOOLEAN](../../../api/ycql/type_bool)                 |         |
| {{<icon/yes>}} | [COUNTER](../../../api/ycql/type_int)                  |         |
| {{<icon/yes>}} | [DATE](../../../api/ycql/type_datetime/)               |         |
| {{<icon/yes>}} | [DECIMAL](../../../api/ycql/type_number)               |         |
| {{<icon/yes>}} | [DOUBLE](../../../api/ycql/type_number)                |         |
| {{<icon/yes>}} | [FLOAT](../../../api/ycql/type_number)                 |         |
| {{<icon/yes>}} | [FROZEN](../../../api/ycql/type_frozen)                |         |
| {{<icon/yes>}} | [INET](../../../api/ycql/type_inet)                    |         |
| {{<icon/yes>}} | [INT, INTEGER](../../../api/ycql/type_int)             |         |
| {{<icon/yes>}} | [JSONB](../../../api/ycql/type_jsonb)                  |         |
| {{<icon/yes>}} | [LIST](../../../api/ycql/type_collection)              |         |
| {{<icon/yes>}} | [MAP](../../../api/ycql/type_collection)               |         |
| {{<icon/yes>}} | [SET](../../../api/ycql/type_collection)               |         |
| {{<icon/yes>}} | [SMALLINT](../../../api/ycql/type_int)                 |         |
| {{<icon/yes>}} | [TEXT, VARCHAR](../../../api/ycql/type_text)           |         |
| {{<icon/yes>}} | [TIME](../../../api/ycql/type_datetime/)               |         |
| {{<icon/yes>}} | [TIMESTAMP](../../../api/ycql/type_datetime/)          |         |
| {{<icon/yes>}} | [TIMEUUID](../../../api/ycql/type_uuid)                |         |
| {{<icon/yes>}} | [TINYINT](../../../api/ycql/type_int)                  |         |
| {{<icon/yes>}} | [User defined Type](../../../api/ycql/ddl_create_type) |         |
| {{<icon/yes>}} | [UUID](../../../api/ycql/type_uuid)                    |         |
| {{<icon/yes>}} | [VARINT](../../../api/ycql/type_int)                   |         |
| {{<icon/no>}}  | TUPLE                                                  |         |
{.sno-1}

### User defined data types

|                |    Operation    |                     Details                      |
| :------------: | :-------------- | ------------------------------------------------ |
| {{<icon/yes>}} | Create new type | [CREATE TYPE](../../../api/ycql/ddl_create_type) |
| {{<icon/yes>}} | Delete types    | [DROP TYPE](../../../api/ycql/ddl_create_type)   |
| {{<icon/no>}}  | Alter types     |                                                  |
{.sno-1}

## DDL

### Keyspaces

|                    |           Operation            |                             Details                             |
| :----------------: | :----------------------------- | :-------------------------------------------------------------- |
|   {{<icon/yes>}}   | Creating keyspace              | [CREATE KEYSPACE](../../../api/ycql/ddl_create_keyspace/)       |
| {{<icon/partial>}} | Modifying keyspace             | [ALTER KEYSPACE](../../../api/ycql/ddl_alter_keyspace/) - No Op |
|   {{<icon/no>}}    | Check before altering keyspace | `ALTER KEYSPACE IF EXISTS`                                      |
|   {{<icon/no>}}    | Check before creating keyspace | `CREATE KEYSPACE IF NOT EXISTS`                                 |
{.sno-1}

### Tables

|                |          Operation           |                                    Details                                     |
| :------------: | :--------------------------- | :----------------------------------------------------------------------------- |
| {{<icon/yes>}} | Adding columns               | [ADD COLUMN](../../../api/ycql/ddl_alter_table#add-a-column-to-a-table)        |
| {{<icon/yes>}} | Altering tables              | [ALTER TABLE](../../../api/ycql/ddl_alter_table/)                              |
| {{<icon/no>}}  | Check before altering tables | `IF EXISTS`                                                                    |
| {{<icon/yes>}} | Removing columns             | [DROP COLUMN](../../../api/ycql/ddl_alter_table/#remove-a-column-from-a-table) |
| {{<icon/yes>}} | Rename column's name         | [RENAME COLUMN](../../../api/ycql/ddl_alter_table/#rename-a-column-in-a-table) |
{.sno-1}

### Indexes

|                |          Operation           |                                              Details                                              |
| :------------: | :--------------------------- | :------------------------------------------------------------------------------------------------ |
| {{<icon/yes>}} | Adding indexes               | [CREATE INDEX](../../../api/ycql/ddl_create_index/)                                               |
| {{<icon/no>}}  | Adding indexes on Collection | Cannot create index on  `map/list/set/full jsonb/udt` and the keys,values,entries of a collection |
| {{<icon/yes>}} | Partial indexes              | [Partial indexes](../../../api/ycql/ddl_create_index#partial-index)                               |
| {{<icon/yes>}} | Covering indexes             | [Covering indexes](../../../api/ycql/ddl_create_index#included-columns)                           |
| {{<icon/yes>}} | Removing indexes             | [DROP INDEX](../../../api/ycql/ddl_drop_index/)                                                   |
| {{<icon/yes>}} | Unique indexes               | [Unique indexes](../../../api/ycql/ddl_create_index#unique-index)                                 |
{.sno-1}

## DML

### Select

|                |                 Operation                 |                                 Details                                  |
| :------------: | :---------------------------------------- | :----------------------------------------------------------------------- |
| {{<icon/yes>}} | Select columns                            | [SELECT * FROM ...](../../../api/ycql/dml_select/)                       |
| {{<icon/yes>}} | Conditional select with `[NOT] IN` clause | [SELECT ... WHERE key IN ...](../../../api/ycql/dml_select#where-clause) |
| {{<icon/yes>}} | Conditional select with `IF` clause       | [SELECT ... IF ...](../../../api/ycql/dml_select#if-clause)              |
| {{<icon/no>}}  | Select using `CONTAINS [KEY]`             | {{<issue 3620 "Tracked">}}                                               |
| {{<icon/no>}}  | `SELECT JSON`                             | But [JSONB](../../../api/ycql/type_jsonb/) is supported as a native type |
| {{<icon/no>}}  | Select with `PER PARTITION LIMIT`         |                                                                          |
| {{<icon/no>}}  | Grouping results with `GROUP BY`          |                                                                          |
{.sno-1}

### Update

|                |              Operation              |                            Details                            |
| :------------: | :---------------------------------- | :------------------------------------------------------------ |
| {{<icon/yes>}} | Update columns                      | [UPDATE](../../../api/ycql/dml_update/)                       |
| {{<icon/yes>}} | Conditional update with `IF` clause | [UPDATE ... IF](../../../api/ycql/dml_update#if-clause)       |
| {{<icon/yes>}} | Update with `USING` clause          | [UPDATE ... USING](../../../api/ycql/dml_update#using-clause) |
{.sno-1}

### Delete

|                |                 Operation                 |                                 Details                                  |
| :------------: | :---------------------------------------- | :----------------------------------------------------------------------- |
| {{<icon/yes>}} | Delete rows                               | [DELETE](../../../api/ycql/dml_delete/)                                  |
| {{<icon/yes>}} | Conditional delete with `IF` clause       | [DELETE ... IF](../../../api/ycql/dml_update#if-clause)                  |
| {{<icon/yes>}} | Conditional delete with `[NOT] IN` clause | [DELETE ... WHERE key IN ...](../../../api/ycql/dml_delete#where-clause) |
| {{<icon/yes>}} | Delete with `USING` clause                | [DELETE ... USING](../../../api/ycql/dml_delete#using-clause)            |
{.sno-1}

### Insert

|                |   Operation    |                                 Details                                  |
| :------------: | :------------- | :----------------------------------------------------------------------- |
| {{<icon/yes>}} | Adding columns | [INSERT ... INTO ...](../../../api/ycql/dml_insert/)                     |
| {{<icon/no>}}  | `INSERT JSON`  | But [JSONB](../../../api/ycql/type_jsonb/) is supported as a native type |
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
| {{<icon/no>}}  | Users        | Legacy Cassandra feature (_CREATE, DROP, ALTER,LIST_)                          |
| {{<icon/yes>}} | Roles        | [Manage users and roles](../../../secure/authorization/create-roles-ycql/)     |
| {{<icon/no>}}  | `LIST ROLES` | But can be done using [query](../../../secure/authorization/create-roles-ycql) |
| {{<icon/yes>}} | Permissions  | [Grant privileges](../../../secure/authorization/ycql-grant-permissions/)      |
| {{<icon/no>}}  | `LIST PERMISSIONS` | But can be done using [query](../../secure/authorization/ycql-grant-permissions/#2-list-permissions-for-roles) |
{.sno-1}

## Other Features

|                    |          Component           |                                    Details                                     |
| :----------------: | :--------------------------- | :----------------------------------------------------------------------------- |
|   {{<icon/yes>}}   | Aggregates                   | [AVG, COUNT, MAX, MIN, SUM](../../../api/ycql/expr_fcall/#aggregate-functions) |
| {{<icon/partial>}} | Batch                        | Only programattically via [BatchStatement](../../../api/ycql/batch/)           |
|   {{<icon/yes>}}   | Built-in Functions           | [Now, DateOf, CurrentTime, ToTime, UUID ... ](../../../api/ycql/expr_fcall/)   |
|   {{<icon/no>}}    | Materialized Views           |                                                                                |
|   {{<icon/yes>}}   | Operators                    | [Binary, Unary, Null operators](../../../api/ycql/expr_ocall/)                 |
|   {{<icon/no>}}    | Triggers                     |                                                                                |
|   {{<icon/no>}}    | User Defined Aggregates(UDA) |                                                                                |
|   {{<icon/no>}}    | User-defined functions(UDF)  |                                                                                |
{.sno-1}

## Learn more

- [Comparison with Apache Cassandra](../../faq/comparisons/cassandra)
- [YCQL command reference](../../api/ycql/)