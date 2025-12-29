---
title: PostgreSQL Anonymizer extension
headerTitle: PostgreSQL Anonymizer extension
linkTitle: Anonymizer
description: Using the PostgreSQL Anonymizer extension in YugabyteDB
menu:
  stable:
    identifier: extension-pganon
    parent: pg-extensions
    weight: 20
type: docs
aliases:
  - /stable/explore/ysql-language-features/pg-extensions/extension-pganon
---

The [PostgreSQL Anonymizer](https://postgresql-anonymizer.readthedocs.io/en/stable/) extension can be used for masking or replacing personally identifiable information (PII) or commercially sensitive data in a YSQL database.

The extension has a declarative approach to anonymization. This means you can declare the masking rules using the PostgreSQL Data Definition Language (DDL) and specify your anonymization policy inside the table definition itself.

YugabyteDB uses v1.3.1 of PostgreSQL Anonymizer.

## Enable Anonymizer

To enable the Anonymizer extension, you set the YB-TServer `--enable_pg_anonymizer` flag to true. For example, using [yugabyted](../../../reference/configuration/yugabyted/), you would do the following:

```sh
./bin/yugabyted start --tserver_flags="enable_pg_anonymizer=true"
```

Note that modifying `--enable_pg_anonymizer` requires restarting the YB-TServer.

## Customize Anonymizer

You can customize the following anon parameters:

| Parameter | Description | Default |
| :--- | :--- | :--- |
| anon.algorithm | The hashing method used by pseudonymizing functions. | sha256 |
| anon.maskschema | The schema (that is, the namespace) where the dynamic masking views will be stored. | mask |
| anon.restrict_to_trusted_schemas | By enabling this parameter, masking rules must be defined using functions located in a limited list of namespaces. | true |
| anon.salt | The salt used by pseudonymizing functions. | (empty) |
| anon.sourceschema | The schema (that is, the namespace) where the tables are masked by the dynamic masking engine. | public |

For more information, refer to [Configuration](https://postgresql-anonymizer.readthedocs.io/en/stable/configure/) in the Anonymizer documentation.

## Create the anon extension

To enable the extension:

```sql
CREATE EXTENSION anon;
```

If you want to use the `anon.fake_*` functions, you need to load the fake data (see [Declare masking rules](#declare-masking-rules)).

```sql
BEGIN;
SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO TRUE;
SELECT anon.init(); -- Loads fake data
COMMIT;
```

## Use Anonymizer

### Declare masking rules

Too use Anonymizer, you first declare a masking policy. A masking policy is a set of masking rules stored inside the database model and applied to various database objects.

You declare data masking rules using [security labels](https://www.postgresql.org/docs/15/sql-security-label.html). For example:

```sql
CREATE TABLE player( id SERIAL, name TEXT, total_points INT, highest_score INT);

INSERT INTO player VALUES
  ( 1, 'Kareem Abdul-Jabbar', 38387, 55),
  ( 5, 'Michael Jordan', 32292, 69);

SECURITY LABEL FOR anon ON COLUMN player.name
  IS 'MASKED WITH FUNCTION anon.fake_last_name()';

SECURITY LABEL FOR anon ON COLUMN player.id
  IS 'MASKED WITH VALUE NULL';
```

Anonymizer provides many different functions that you can use to declare the masking rules. For a list of masking functions, refer to [Masking functions](https://postgresql-anonymizer.readthedocs.io/en/stable/masking_functions/) in the Anonymizer documentation.

Note that YugabyteDB does not currently support the `anon.dummy_` functions.

Refer to [Declare masking rules](https://postgresql-anonymizer.readthedocs.io/en/stable/declare_masking_rules/) in the Anonymizer documentation for more information.

### Dynamic masking

To enable dynamic masking:

```sql
SELECT anon.start_dynamic_masking(false);
```

You must run this every time a masked security label is created for a user or role.

The boolean parameter indicates whether fake data should be loaded or not. It is recommended to use `anon.init()` to load fake data. This creates masked views on the `anon.maskschema` for all the tables present in `anon.sourceschema`, and alters the privileges of all users with a masked security label so that a masked user will only be able to read masked data and not the original data.

To check if a role with a masked security label will see masked values, use the following query:

```sql
SELECT rolname, rolconfig FROM pg_roles WHERE rolname = '<role_name>'
```

If `rolconfig` contains `search_path=<anon.maskschema>, <anon.sourceschema>`, then the masked user will see masked values.

The following shows output where the `anon.maskschema` and `anon.sourceschema` parameters are set to their default values:

```output
 rolname |          rolconfig
---------+------------------------------
 skynet  | {"search_path=mask, public"}
```

Note that [Backup and restore](../../../manage/backup-restore/) doesn't preserve roles, and will also not restore masked security labels for roles. After a restore, you will need to manually recreate security labels for roles, and then enable dynamic masking.

To disable dynamic masking:

```sql
SELECT anon.stop_dynamic_masking();
```

This drops all the masked views, unmasks all the masked roles, and drops `anon.maskschema`.

### Static masking

After declaring masking rules for columns, you can use the following parameters to define masks:

- `anon.anonymize_column(<table-name>, <column-name>)` - Anonymize the column of the table with the declared masking rule.
- `anon.anonymize_table(<table-name>)` - Anonymize all the columns with masking rules of the table.
- `anon.anonymize_database(<database-name>)` - Anonymize all the columns with masking rules of all the tables in the given database.

With static masking, the data is lost forever once it is anonymized. Refer to [Static masking](https://postgresql-anonymizer.readthedocs.io/en/stable/static_masking/) in the Anonymizer documentation for more information.

## Examples

### Dynamically mask contact information

```sql
CREATE EXTENSION anon;

BEGIN;
SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;
SELECT anon.init(); -- Loads fake data
COMMIT;

CREATE TABLE people ( id TEXT, firstname TEXT, lastname TEXT, phone TEXT);
INSERT INTO people VALUES ('1', 'John', 'Doe','1234567890');
SELECT * FROM people; -- non masked user can read original values
```

```output
 id | firstname | lastname |   phone
----+-----------+----------+------------
 1  | John      | Doe      | 1234567890
```

```sql
SECURITY LABEL FOR anon ON COLUMN people.phone
IS 'MASKED WITH FUNCTION anon.partial(phone, 2, $$******$$, 2)';

CREATE ROLE skynet LOGIN;
SECURITY LABEL FOR anon ON ROLE skynet IS 'MASKED';
GRANT SELECT ON ALL TABLES IN SCHEMA public TO skynet;

\c yugabyte skynet

SELECT * FROM people;
```

```output
 id | firstname | lastname |   phone
----+-----------+----------+------------
 1  | John      | Doe      | 1234567890
```

Note how, as we have not yet started dynamic masking, the data is not masked for the masked user.

```sql
\c yugabyte yugabyte

SELECT anon.start_dynamic_masking(false);

\c yugabyte skynet

SELECT * FROM people;
```

```output
 id | firstname | lastname |   phone
----+-----------+----------+------------
 1  | John      | Doe      | 12******90
```

### Prevent a user from reading values from the source table

First set the cluster-wide value of `anon.sourcesschema` to `test`:

```sh
--ysql_pg_conf_csv=shared_preload_libraries=anon,anon.sourceschema=test
```

Execute the following commands:

```sql
CREATE EXTENSION anon;

BEGIN;
SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;
SELECT anon.init(); -- Loads fake data
COMMIT;

CREATE SCHEMA test;

CREATE TABLE test.people ( id TEXT, firstname TEXT, lastname TEXT, phone TEXT);
INSERT INTO test.people VALUES ('1', 'John', 'Doe','1234567890');

CREATE ROLE skynet LOGIN;
SECURITY LABEL FOR anon ON ROLE skynet IS 'MASKED';

SECURITY LABEL FOR anon ON COLUMN test.people.phone
IS 'MASKED WITH FUNCTION anon.partial(phone, 2, $$******$$, 2)';

SELECT anon.start_dynamic_masking(false);

\c yugabyte skynet

SELECT * FROM people;
```

```output
 id | firstname | lastname |   phone
----+-----------+----------+------------
 1  | John      | Doe      | 12******90
```

```sql
SELECT * FROM test.people;
```

```output
ERROR:  permission denied for schema test
LINE 1: SELECT * FROM test.people;
```

### Statically mask contact information

```sql
CREATE EXTENSION anon;

BEGIN;
SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;
SELECT anon.init();
COMMIT;

CREATE TABLE people ( id TEXT, firstname TEXT, lastname TEXT, phone TEXT);
INSERT INTO people VALUES ('1', 'John', 'Doe','1234567890');

SECURITY LABEL FOR anon ON COLUMN people.lastname
IS 'MASKED WITH FUNCTION anon.fake_last_name()';

SECURITY LABEL FOR anon ON COLUMN people.phone
IS 'MASKED WITH FUNCTION anon.partial(phone, 2, $$******$$, 2)';

SELECT anon.anonymize_table('people');

SELECT * FROM people;
```

```output
 id | firstname | lastname |   phone
----+-----------+----------+------------
 1  | John      | Bryant   | 12******90
```

### Mask without faking

```sql
CREATE EXTENSION anon;

CREATE TABLE people ( id TEXT, firstname TEXT, lastname TEXT, phone TEXT);
INSERT INTO people VALUES ('1', 'John', 'Doe', '1234567890');

SECURITY LABEL FOR anon ON COLUMN people.firstname
IS 'MASKED WITH VALUE $$CONFIDENTIAL$$';

SECURITY LABEL FOR anon ON COLUMN people.lastname
IS 'MASKED WITH VALUE $$CONFIDENTIAL$$';

SECURITY LABEL FOR anon ON COLUMN people.phone
IS 'MASKED WITH FUNCTION anon.partial(phone, 2, $$******$$, 2)';

CREATE ROLE skynet LOGIN;
SECURITY LABEL FOR anon ON ROLE skynet IS 'MASKED';

SELECT anon.start_dynamic_masking(false);

\c yugabyte skynet

SELECT * FROM people;
```

```output
 id |  firstname   |   lastname   |   phone
----+--------------+--------------+------------
 1  | CONFIDENTIAL | CONFIDENTIAL | 12******90
```

## Limitations

- Masking views and materialized views are not supported.
- The SECURITY LABEL commands on tables and databases are not supported.
- YugabyteDB does not currently support the `anon.dummy_` functions.

Refer to [Masking rule limitations](https://postgresql-anonymizer.readthedocs.io/en/stable/declare_masking_rules/#limitations) and [Legacy masking rule limitations](https://postgresql-anonymizer.readthedocs.io/en/stable/legacy_dynamic_masking/#limitations) in the Anonymizer documentation for information on the Anonymizer extension limitations.
