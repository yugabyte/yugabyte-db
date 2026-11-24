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

YugabyteDB uses v3.1.3 of PostgreSQL Anonymizer. Earlier YugabyteDB versions use v1.3.1. To move from v1.3.1, see [Upgrade from Anonymizer 1.x](#upgrade-from-anonymizer-1-x).

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
| anon.dummy_locale | The default locale for the `anon.dummy_*` functions. | en_US |
| anon.masking_policies | Additional masking policies. The `anon` policy is always defined. | (empty) |
| anon.privacy_by_default | Mask every column that has no masking rule with NULL (or the column default for NOT NULL columns). | off |
| anon.restrict_to_trusted_schemas | By enabling this parameter, masking rules must be defined using functions located in a limited list of namespaces. | on |
| anon.salt | The salt used by pseudonymizing functions. | (empty) |
| anon.strict_mode | A masking rule can't change the data type of a column. | on |
| anon.transparent_dynamic_masking | Turns on [dynamic masking](#dynamic-masking). | off |

For more information, refer to [Configuration](https://postgresql-anonymizer.readthedocs.io/en/stable/configure/) in the Anonymizer documentation.

## Create the anon extension

Only a superuser, or a member of the `yb_extension` role, can create the extension:

```sql
CREATE EXTENSION anon;
```

If you want to use the `anon.fake_*` functions, you need to load the fake data (see [Declare masking rules](#declare-masking-rules)):

```sql
SELECT anon.init(); -- Loads fake data
```

`anon.init()` prints a `ROWS_PER_TRANSACTION is not supported in a transaction block` warning for each table it loads. You can ignore these warnings.

## Use Anonymizer

### Declare masking rules

To use Anonymizer, you first declare a masking policy. A masking policy is a set of masking rules stored inside the database model and applied to various database objects.

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

Refer to [Declare masking rules](https://postgresql-anonymizer.readthedocs.io/en/stable/declare_masking_rules/) in the Anonymizer documentation for more information.

### Dynamic masking

With dynamic masking, roles that you declare as masked see masked values when they query a table. Other roles see the original data. The data in the table doesn't change.

To use dynamic masking:

1. Turn on dynamic masking for the database:

    ```sql
    ALTER DATABASE <database_name> SET anon.transparent_dynamic_masking TO true;
    ```

    The setting applies to new connections.

1. Declare a role as masked:

    ```sql
    SECURITY LABEL FOR anon ON ROLE <role_name> IS 'MASKED';
    ```

1. Grant the masked role `SELECT` on the tables it reads. A masked role queries the tables directly, so it needs its own privileges on them:

    ```sql
    GRANT SELECT ON <table_name> TO <role_name>;
    ```

1. If your masking rules use the `anon.fake_*` functions, also grant the masked role access to the fake data:

    ```sql
    GRANT USAGE ON SCHEMA anon TO <role_name>;
    GRANT SELECT ON ALL TABLES IN SCHEMA anon TO <role_name>;
    ```

Masking rules declared on a table are not applied when a masked role reads the table through a view. To mask data read through a view, declare masking rules on the columns of the view.

Note that [Backup and restore](../../../manage/backup-restore/) doesn't preserve roles, and will also not restore masked security labels for roles. After a restore, you will need to manually recreate the security labels for roles.

To turn off dynamic masking for a database:

```sql
ALTER DATABASE <database_name> RESET anon.transparent_dynamic_masking;
```

For more information, refer to [Dynamic masking](https://postgresql-anonymizer.readthedocs.io/en/stable/dynamic_masking/) in the Anonymizer documentation.

### Static masking

After declaring masking rules for columns, you can use the following functions to replace the data with masked values:

- `anon.anonymize_column(<table-name>, <column-name>)` - Anonymize the column of the table with the declared masking rule.
- `anon.anonymize_table(<table-name>)` - Anonymize all the columns with masking rules of the table.
- `anon.anonymize_database()` - Anonymize all the columns with masking rules of all the tables in the current database.

With static masking, the data is lost forever once it is anonymized. Refer to [Static masking](https://postgresql-anonymizer.readthedocs.io/en/stable/static_masking/) in the Anonymizer documentation for more information.

## Examples

### Dynamically mask contact information

```sql
CREATE EXTENSION anon;

CREATE TABLE people ( id TEXT, firstname TEXT, lastname TEXT, phone TEXT);
INSERT INTO people VALUES ('1', 'John', 'Doe','1234567890');

SECURITY LABEL FOR anon ON COLUMN people.phone
IS 'MASKED WITH FUNCTION anon.partial(phone, 2, $$******$$, 2)';

CREATE ROLE skynet LOGIN;
SECURITY LABEL FOR anon ON ROLE skynet IS 'MASKED';
GRANT SELECT ON people TO skynet;

\c yugabyte skynet

SELECT * FROM people;
```

```output
 id | firstname | lastname |   phone
----+-----------+----------+------------
 1  | John      | Doe      | 1234567890
```

Note how, as dynamic masking is not yet turned on, the data is not masked for the masked user.

```sql
\c yugabyte yugabyte

ALTER DATABASE yugabyte SET anon.transparent_dynamic_masking TO true;

\c yugabyte skynet

SELECT * FROM people;
```

```output
 id | firstname | lastname |   phone
----+-----------+----------+------------
 1  | John      | Doe      | 12******90
```

A role that is not masked still sees the original data:

```sql
\c yugabyte yugabyte

SELECT * FROM people;
```

```output
 id | firstname | lastname |   phone
----+-----------+----------+------------
 1  | John      | Doe      | 1234567890
```

### Statically mask contact information

```sql
CREATE EXTENSION anon;

SELECT anon.init();

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

The last name is a random value from the fake data, so it differs from run to run.

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
GRANT SELECT ON people TO skynet;

ALTER DATABASE yugabyte SET anon.transparent_dynamic_masking TO true;

\c yugabyte skynet

SELECT * FROM people;
```

```output
 id |  firstname   |   lastname   |   phone
----+--------------+--------------+------------
 1  | CONFIDENTIAL | CONFIDENTIAL | 12******90
```

## Upgrade from Anonymizer 1.x

YugabyteDB doesn't support `ALTER EXTENSION anon UPDATE` from v1.3.1 to v3.1.3. To move to v3.1.3, you drop the v1.3.1 extension and create the v3.1.3 extension. `DROP EXTENSION anon` keeps the security labels, so the masking rules on columns and the masked roles are kept.

v3.1.3 removes the v1.3.1 dynamic masking engine, including `anon.start_dynamic_masking()`, `anon.stop_dynamic_masking()`, the masking views, and the `anon.maskschema` and `anon.sourceschema` parameters. Dynamic masking is turned on with `anon.transparent_dynamic_masking` instead.

{{< warning title="Masked roles are not masked during the upgrade" >}}
From the time you stop dynamic masking until you turn it on again with v3.1.3, masked roles are not masked. They get a permission error, or read the original data if they have `SELECT` on the table. Block masked roles from logging in during this window. Other roles are not affected.
{{< /warning >}}

### Upgrade from v2025.1, v2025.2, or v2026.1

1. Before the upgrade, run the following in each database that uses Anonymizer dynamic masking:

    ```sql
    SELECT anon.stop_dynamic_masking();
    ```

1. Upgrade the cluster.

1. Drop and recreate the extension in each database that has it. `CASCADE` is not needed, because step 1 removed the masking views.

    ```sql
    DROP EXTENSION anon;
    CREATE EXTENSION anon;
    SELECT anon.init();
    ```

1. Turn on dynamic masking:

    ```sql
    ALTER DATABASE <database_name> SET anon.transparent_dynamic_masking TO true;
    ```

1. Grant `SELECT` on the tables to every masked role that didn't have it before, and access to the fake data if your rules use the `anon.fake_*` functions (see [Dynamic masking](#dynamic-masking)). In v1.3.1, masked roles read the masking views, and the views read the tables with their owner's privileges. In v3.1.3, masked roles query the tables directly.

If you upgraded without running `anon.stop_dynamic_masking()` first, it fails after the upgrade with `unrecognized configuration parameter "anon.sourceschema"`, and masked roles still use the v1.3.1 masking views. Undo its work by hand in each database that used dynamic masking:

1. List the masked roles:

    ```sql
    SELECT r.rolname FROM pg_shseclabel l JOIN pg_roles r ON r.oid = l.objoid
      WHERE l.provider = 'anon' AND l.label ILIKE 'MASKED';
    ```

1. Reset the search path of each masked role:

    ```sql
    ALTER ROLE <role_name> RESET search_path;
    ```

1. Drop the masking views and their schema (`mask`, unless you changed `anon.maskschema`):

    ```sql
    DROP SCHEMA mask CASCADE;
    ```

1. Continue from step 3 of the upgrade.

### Upgrade from v2024.2 or earlier

A [YSQL major upgrade](../../../manage/ysql-major-upgrade-yugabyted/) refuses to start while Anonymizer v1.x or any of its security labels are installed. The check lists the labels, and writes two files to the upgrade's output directory:

- `anon_security_labels.txt` - statements that remove the labels.
- `anon_security_labels_restore.txt` - statements that recreate the labels after the upgrade.

Before the upgrade, in this order:

1. In every database that uses dynamic masking, run `SELECT anon.stop_dynamic_masking();`. It finds the masked roles through their labels, so run it before you remove any label.
1. Run `DROP EXTENSION anon` in every database that has it.
1. Remove the security labels with the statements in `anon_security_labels.txt`. Until all are removed, keep `--enable_pg_anonymizer` on, and keep every listed label provider in `anon.masking_policies` (or `anon.k_anonymity_provider` for k-anonymity labels) in `--ysql_pg_conf_csv` on every YB-TServer. Restart the YB-TServers after changing them.

Then run the upgrade again. After the upgrade:

1. Run `CREATE EXTENSION anon` in each database, and `SELECT anon.init()` if your rules use the `anon.fake_*` functions.
1. Rewrite masking rules that call functions v3.1.3 no longer has.
1. Run the statements in `anon_security_labels_restore.txt`.
1. Grant masked roles `SELECT` on the tables they read and on the fake data (see [Dynamic masking](#dynamic-masking)).
1. Turn on dynamic masking with `ALTER DATABASE <database_name> SET anon.transparent_dynamic_masking TO true`.

## Limitations

- Masked roles can't run `EXPLAIN` or `TRUNCATE`.
- Masking rules on a table are not applied to reads through a view. Declare masking rules on the view's columns instead.
- Sampling rules (`SECURITY LABEL FOR anon ON TABLE ... IS 'TABLESAMPLE ...'`) can be declared, but YugabyteDB doesn't support `TABLESAMPLE` ({{<issue 4288>}}), so masked reads, static masking, and `ysql_dump` of a sampled table fail.
- Replica masking (`anon.replica_masking`) is not supported, because YugabyteDB doesn't support logical replication subscriptions.

Refer to [Masking rule limitations](https://postgresql-anonymizer.readthedocs.io/en/stable/declare_masking_rules/#limitations) in the Anonymizer documentation for information on the Anonymizer extension limitations.
