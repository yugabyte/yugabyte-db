---
title: Schema review workarounds for YugabyteDB Voyager
headerTitle: Schema review workarounds
linkTitle: Schema review workarounds
headcontent: What to watch out for when migrating data using YugabyteDB Voyager
description: What to watch out for when migrating data using YugabyteDB Voyager.
type: indexpage
showRightNav: true
aliases:
  - /preview/yugabyte-voyager/known-issues/general-issues/
  - /preview/yugabyte-voyager/known-issues/mysql-oracle/
menu:
  preview_yugabyte-voyager:
    identifier: known-issues
    parent: yugabytedb-voyager
    weight: 104
---

Review the unsupported features, limitations, and implement suggested workarounds to successfully migrate data from MySQL, Oracle, or PostgreSQL to YugabyteDB.

## Unsupported features

Currently, yb-voyager doesn't support the following features:

| Feature | Description/Alternatives  | GitHub Issue |
| :------ | :------------------------ | :----------- |
| ALTER VIEW | YugabyteDB does not yet support any schemas containing `ALTER VIEW` statements. | [48](https://github.com/yugabyte/yb-voyager/issues/48) |
| USERS/GRANTS | Voyager does not support migrating the USERS and GRANTS from the source database to the target cluster. | |
| CREATE CAST<br>CREATE SERVER <br> CREATE&nbsp;ACCESS&nbsp;METHOD | Voyager does not support migrating these object types from the source database to the target cluster. | |
| Unsupported data types | Data migration is unsupported for some data types, such as BLOB and XML. For others such as ANY and BFile, both schema and data migration is unsupported. Refer to [datatype mapping](../reference/datatype-mapping-oracle/) for the detailed list of data types. | |
| Unsupported PostgreSQL features | Yugabyte currently doesn't support the PostgreSQL features listed in [PostgreSQL compatibility](../../develop/postgresql-compatibility/#unsupported-postgresql-features). If such schema clauses are encountered, Voyager results in an error. | |

## Schema review

{{<index/block>}}

  {{<index/item
    title="PostgreSQL"
    body="Explore workarounds for limitations associated with PostgreSQL as the source database."
    href="postgresql/"
    icon="/images/section_icons/architecture/concepts.png">}}

  {{<index/item
    title="Oracle"
    body="Explore workarounds for limitations associated with Oracle as the source database."
    href="oracle/"
    icon="/images/section_icons/architecture/concepts.png">}}

  {{<index/item
    title="MySQL"
    body="Explore workarounds for limitations associated with MySQL as the source database."
    href="mysql/"
    icon="/images/section_icons/architecture/concepts.png">}}

{{</index/block>}}

## Assess and Analyze Limitations

There are certain limitations when reporting issues in [assess-migration](../reference/assess-migration/) and [analyze-schema](../reference/schema-migration/analyze-schema/) commands:

1. Normalized queries in `pg_stat_statements`

    The `pg_stat_statements` extension in PostgreSQL tracks execution statistics of SQL queries by normalizing them. Since normalization removes the constants, the issues related to the constants for the following scenarios cannot be detected during assessment:

    - `JSON_TABLE` ([JSON Query function](../known-issues/postgresql/#postgresql-12-and-later-features)) usage in DML statements.
    - [Non-decimal integer literals in DML statements.](../known-issues/postgresql/#postgresql-12-and-later-features)
    - [Two-Phase Commit (XA syntax)](../known-issues/postgresql/#two-phase-commit).

    **Example:**

    ```sql
    --query stored in pg_stat_statements:
    SELECT $1 as binary; 

    --actual query:
    SELECT 0b101010 as binary;
    ```

1. Transactional Queries in `pg_stat_statements`

    In `pg_stat_statements`, a single transaction is recorded as multiple separate query entries. This fragmentation makes it challenging to detect issues that occur within transaction boundaries, such as:

    - [DDL operations within Transaction](../known-issues/postgresql/#ddl-operations-within-the-transaction)

    **Example:**

    ```sql
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

1. Determining the Type During Query Processing

    In complex queries, determining the type of data being handled is not always straightforward. This limitation affects the detection of [JSONB subscripting](../known-issues/postgresql/#jsonb-subscripting) in such queries, making it difficult to report issues accurately.

    **Example:**

    ```sql
    select ab_data['name'] from (select data as ab_data from test_jsonb); --data is the JSONB column in test_jsonb table
    ```

1. Parser Limitations

    The internal [Golang PostgreSQL parser](https://github.com/pganalyze/pg_query_go) used for schema analysis and migration assessment has certain limitations within PL/pgSQL blocks. This leads to not being able to detect issues on the statements embedded inside: Expressions, Conditions, Assignments, Loop variables, Function call arguments, and so on.

    **Example:**

    ```sql
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

1. Global Objects

    Global objects in PostgreSQL are database-level objects that are not tied to a specific schema. Some of these following objects are not fully supported in schema analysis and migration assessments for detecting issues.

    **Example:**

    - `CREATE ACCESS METHOD` / `ALTER ACCESS METHOD`
    - `CREATE SERVER`
    - `CREATE DATABASE` with certain options (for example, `ICU_LOCALE`, `LOCALE_PROVIDER`)
