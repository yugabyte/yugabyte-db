---
title: Schema review workarounds for YugabyteDB Voyager
headerTitle: Schema review workarounds
linkTitle: Schema review workarounds
headcontent: What to watch out for when migrating data using YugabyteDB Voyager
description: What to watch out for when migrating data using YugabyteDB Voyager.
type: indexpage
showRightNav: true
aliases:
  - /stable/yugabyte-voyager/known-issues/general-issues/
  - /stable/yugabyte-voyager/known-issues/mysql-oracle/
menu:
  stable_yugabyte-voyager:
    identifier: known-issues
    parent: yugabytedb-voyager
    weight: 104
---

The following sections provide workarounds for issues detected by the Voyager [migration assessment](../reference/assess-migration/) and [schema analysis](../reference/schema-migration/analyze-schema/) commands.

Review the unsupported features and limitations, and implement the suggested workarounds to successfully migrate data from MySQL, Oracle, or PostgreSQL to YugabyteDB.

- [Workarounds for PostgreSQL issues](./postgresql/)
- [Workarounds for Oracle issues](./oracle/)
- [Workarounds for MySQL issues](./mysql/)

## Unsupported features

Currently, yb-voyager doesn't support the following features:

| Feature | Description/Alternatives  | GitHub Issue |
| :------ | :------------------------ | :----------- |
| ALTER VIEW | YugabyteDB does not yet support any schemas containing `ALTER VIEW` statements. | [48](https://github.com/yugabyte/yb-voyager/issues/48) |
| USERS/GRANTS | Voyager does not support migrating the USERS and GRANTS from the source database to the target cluster. | |
| CREATE CAST<br>CREATE SERVER <br> CREATE&nbsp;ACCESS&nbsp;METHOD | Voyager does not support migrating these object types from the source database to the target cluster. | |
| Unsupported data types | Data migration is unsupported for some data types, such as BLOB and XML. For others such as ANY and BFile, both schema and data migration is unsupported. Refer to [datatype mapping](../reference/datatype-mapping-oracle/) for the detailed list of data types. | |
| Unsupported PostgreSQL features | Yugabyte currently doesn't support the PostgreSQL features listed in [PostgreSQL compatibility](../../reference/configuration/postgresql-compatibility/#unsupported-postgresql-features). If such schema clauses are encountered, Voyager results in an error. | |

## Assessment and schema analysis limitations

Although [migration assessment](../reference/assess-migration/) and [schema analysis](../reference/schema-migration/analyze-schema/) detect most issues you may face when migrating a database so that you can work around and mitigate them, the commands have the following known limitations.

### Normalized queries in pg_stat_statements

The pg_stat_statements extension in PostgreSQL tracks execution statistics of SQL queries by normalizing them. Because normalization removes the constants, migration assessment can't detect issues related to the constants for the following scenarios:

- `JSON_TABLE` ([JSON Query function](../known-issues/postgresql/#postgresql-12-and-later-features)) usage in DML statements
- [Non-decimal integer literals in DML statements](../known-issues/postgresql/#postgresql-12-and-later-features)
- [Two-Phase Commit (XA syntax)](../known-issues/postgresql/#two-phase-commit)

For example:

```output.sql
--query stored in pg_stat_statements:
SELECT $1 as binary;

--actual query:
SELECT 0b101010 as binary;
```

### Transactional queries in pg_stat_statements

pg_stat_statements records a single transaction as multiple separate query entries. This fragmentation makes it challenging to detect issues that occur within transaction boundaries, such as:

- [DDL operations within Transaction](../known-issues/postgresql/#ddl-operations-within-the-transaction)

**Example:**

```output.sql
yugabyte=# \d test
Did not find any relation named "test".
yugabyte=# BEGIN;
BEGIN
yugabyte=*# CREATE TABLE test(id int, val text);
CREATE TABLE
yugabyte=*# \d test
                Table "public.test"
Column |  Type   | Collation | Nullable | Default
--------+---------+-----------+----------+---------
id     | integer |           |          |
val    | text    |           |          |
yugabyte=*# ROLLBACK;
ROLLBACK
yugabyte=# \d test
                Table "public.test"
Column |  Type   | Collation | Nullable | Default
--------+---------+-----------+----------+---------
id     | integer |           |          |
val    | text    |           |          |
```

### Determining the type during query processing

In complex queries, determining the type of data being handled is not always straightforward. This limitation affects the detection of [JSONB subscripting](../known-issues/postgresql/#jsonb-subscripting) in such queries, making it difficult to report issues accurately.

For example:

```output.sql
select ab_data['name'] from (select data as ab_data from test_jsonb);
# data is the JSONB column in test_jsonb table
```

### Parser limitations

The internal [Golang PostgreSQL parser](https://github.com/pganalyze/pg_query_go) used for schema analysis and migration assessment has certain limitations within PL/pgSQL blocks. This leads to not being able to detect issues on the statements embedded inside Expressions, Conditions, Assignments, Loop variables, Function call arguments, and so on.

For example:

```output.sql
CREATE OR REPLACE FUNCTION example()
RETURNS VOID AS $$
DECLARE
    x TEXT; -- Adjust the type based on jsonb value type
BEGIN
    x := (SELECT jsonb_column['value'] FROM orders WHERE id = 1);
    -- Further processing logic here
    RAISE NOTICE 'Extracted value: %', x;
END;
$$ LANGUAGE plpgsql;
```

### Global objects

Global objects in PostgreSQL are database-level objects that are not tied to a specific schema. As a result, schema analysis and migration assessment are unable to detect issues in the following global objects:

- `CREATE ACCESS METHOD` / `ALTER ACCESS METHOD`
- `CREATE SERVER`
- `CREATE DATABASE` with certain options (for example, `ICU_LOCALE`, `LOCALE_PROVIDER`)
