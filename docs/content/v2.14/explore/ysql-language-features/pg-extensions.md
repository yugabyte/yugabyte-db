---
title: PostgreSQL extensions
headerTitle: PostgreSQL extensions
linkTitle: PostgreSQL extensions
description: Summary of supported PostgreSQL extensions
summary: Reference for YSQL extensions
menu:
  v2.14:
    identifier: explore-ysql-postgresql-extensions
    parent: explore-ysql-language-features
    weight: 4400
type: docs
---

PostgreSQL extensions provide a way to extend the functionality of a database by bundling SQL objects into a package and using them as a unit.
This page describes the PostgreSQL extensions supported by YugabyteDB.

## PostgreSQL extensions supported by YugabyteDB

Extensions are either pre-bundled with YugabyteDB, or require installation:

* **Pre-bundled** extensions are included in the standard YugabyteDB distribution and can be enabled in YSQL by running the [CREATE EXTENSION](../../../api/ysql/the-sql-language/statements/ddl_create_extension/) statement.
* **Requires installation** - you must install these extensions manually before you can enable them using CREATE EXTENSION. Refer to [Install extensions](#install-extensions).

For information about using a specific extension in YugabyteDB, use the Example links in the following tables.

### PostgreSQL modules

YugabyteDB supports the following [PostgreSQL modules](https://www.postgresql.org/docs/11/contrib.html), all of which are pre-bundled.

| Module | Status | Description | Examples |
| :----- | :----- | :---------- | :------ |
| [auto_explain](#auto-explain) | Pre-bundled | Provides a means for logging execution plans of slow statements automatically. | [Example](#auto-explain-example) |
| [file_fdw](https://www.postgresql.org/docs/11/file-fdw.html) | Pre-bundled | Provides the foreign-data wrapper file_fdw, which can be used to access data files in the server's file system. | [Example](#file-fdw-example) |
| [fuzzystrmatch](https://www.postgresql.org/docs/11/fuzzystrmatch.html) | Pre-bundled | Provides several functions to determine similarities and distance between strings. | [Example](#fuzzystrmatch-example) |
| [hstore](https://www.postgresql.org/docs/11/hstore.html) | Pre-bundled | Implements the hstore data type for storing sets of key-value pairs in a single PostgreSQL value. | |
| [passwordcheck](https://www.postgresql.org/docs/11/passwordcheck.html) | Pre-bundled | Checks user passwords whenever they are set with CREATE ROLE or ALTER ROLE. If a password is considered too weak, it is rejected. | [Example](#passwordcheck-example) |
| [pgcrypto](https://www.postgresql.org/docs/11/pgcrypto.html)| Pre-bundled | Provides various cryptographic functions. | [Example](#pgcrypto-example) |
| [pg_stat_statements](https://www.postgresql.org/docs/11/pgstatstatements.html) | Pre-bundled| Provides a means for tracking execution statistics of all SQL statements executed by a server. | [Example](#pg-stat-statements-example) |
| [pg_trgm](https://www.postgresql.org/docs/11/pgtrgm.html) | Pre-bundled | Provides functions and operators for determining the similarity of alphanumeric text based on trigram matching, as well as index operator classes that support fast searching for similar strings. | |
| [postgres_fdw](https://www.postgresql.org/docs/11/postgres-fdw.html) | Pre-bundled | Provides the foreign-data wrapper postgres_fdw, which can be used to access data stored in external PostgreSQL servers. | [Example](#postgres-fdw-example) |
| [spi](https://www.postgresql.org/docs/11/contrib-spi.html)|Pre-bundled | Lets you use the Server Programming Interface (SPI) to create user-defined functions and stored procedures in C, and to run YSQL queries directly against YugabyteDB. | [Example](#spi-example) |
| [sslinfo](https://www.postgresql.org/docs/11/sslinfo.html) | Pre-bundled | Provides information about the SSL certificate that the current client provided when connecting to PostgreSQL. | |
| [tablefunc](https://www.postgresql.org/docs/11/tablefunc.html) | Pre-bundled | Provides several table functions. For example, `normal_rand()` creates values, picked using a pseudorandom generator, from an ideal normal distribution. You specify how many values you want, and the mean and standard deviation of the ideal distribution. You use it in the same way that you use `generate_series()` | [Example](#tablefunc-example) |
| [uuid-ossp](https://www.postgresql.org/docs/11/uuid-ossp.html) | Pre-bundled | Provides functions to generate universally unique identifiers (UUIDs), and functions to produce certain special UUID constants. | [Example](#uuid-ossp-example) |

### Other extensions

| Extension | Status | Description | Examples |
| :-------- | :----- | :---------- | :------ |
| [pg_hint_plan](https://pghintplan.osdn.jp/pg_hint_plan.html) | Pre-bundled | Tweak execution plans using "hints", which are descriptions in the form of SQL comments. | [Example](../../query-1-performance/pg-hint-plan/#root) |
| [PGAudit](https://www.pgaudit.org/) | Pre-bundled | The PostgreSQL Audit Extension (pgAudit) provides detailed session and/or object audit logging via the standard PostgreSQL logging facility. | [Install and example](../../../secure/audit-logging/audit-logging-ysql/) |
| [pg_stat_monitor](https://github.com/percona/pg_stat_monitor) | Pre-bundled | A PostgreSQL query performance monitoring tool, based on the PostgreSQL pg_stat_statements module. | |
| [Orafce](https://github.com/orafce/orafce)| Pre-bundled | Provides compatibility with Oracle functions and packages that are either missing or implemented differently in YugabyteDB and PostgreSQL. This compatibility layer can help you port your Oracle applications to YugabyteDB. | |
| [PostGIS](https://postgis.net/) | Requires installation | A spatial database extender for PostgreSQL-compatible object-relational databases. | [Install and example](#postgis-example) |
| [postgresql-hll](https://github.com/citusdata/postgresql-hll) | Requires installation | Introduces the data type `hll`, which is a HyperLogLog data structure. | [Install and example](#postgresql-hll-example) |
| [pgsql-postal](https://github.com/pramsey/pgsql-postal) | Requires installation | Parse and normalize street addresses around the world using libpostal. | [Install and example](#pgsql-postal-example)|

## Install extensions

If an extension is not pre-bundled, you need to install it manually before you can enable it using the [CREATE EXTENSION](../../../api/ysql/the-sql-language/statements/ddl_create_extension/) statement. You can install only extensions that are supported by YugabyteDB.

Currently, in a multi-node setup, you need to install the extension on _every_ node in the cluster.

In a read replica setup, install extensions on the primary instance, not on the read replica. Once installed, the extension replicates to the read replica.

You cannot install new extensions in YugabyteDB Managed. If you need a database extension that is not pre-bundled with YugabyteDB added to a YugabyteDB Managed cluster, contact {{% support-cloud %}} with the names of the cluster and extension, or [reach out on Slack](https://yugabyte-db.slack.com/).

### Install an extension

Typically, extensions need three types of files:

* Shared library files (`<name>.so`)
* SQL files (`<name>--<version>.sql`)
* Control files (`<name>.control`)

To install an extension, you need to copy these files into the respective directories of your YugabyteDB installation.

Shared library files go in the `pkglibdir` directory, while SQL and control files go in the `extension` subdirectory of the `libdir` directory.

You can obtain the installation files for the target extension in two ways:

* Build the extension from scratch following the extension's build instructions.
* Copy the files from an existing PostgreSQL installation.

After copying the files, restart the cluster (or the respective node in a multi-node install).

### Locate installation directories using `pg_config`

To find the directories where you install the extension files on your local installation, use the YugabyteDB `pg_config` executable.

First, alias it to `yb_pg_config` by replacing `<yugabyte-path>` with the path to your YugabyteDB installation as follows:

```sh
$ alias yb_pg_config=/<yugabyte-path>/postgres/bin/pg_config
```

List existing shared libraries with:

```sh
$ ls "$(yb_pg_config --pkglibdir)"
```

List SQL and control files for already-installed extensions with:

```sh
$ ls "$(yb_pg_config --sharedir)"/extension/
```

### Copying extensions from PostgreSQL

The easiest way to install an extension is to copy the files from an existing PostgreSQL installation.

Ideally, use the same version of the PostgreSQL extension as that used by YugabyteDB. To see the version of PostgreSQL used in your YugabyteDB installation, enter the following `ysqlsh` command:

```sh
$ ./bin/ysqlsh --version
```

```output
psql (PostgreSQL) 11.2-YB-2.11.2.0-b0
```

If you already have PostgreSQL (use version `11.2` for best YSQL compatibility) with the extension installed, you can find the extension's files as follows:

```sh
$ ls "$(pg_config --pkglibdir)" | grep <name>
```

```sh
$ ls "$(pg_config --sharedir)"/extension/ | grep <name>
```

If you have multiple PostgreSQL versions installed, make sure you're selecting the correct `pg_config`. On an Ubuntu 18.04 environment with multiple PostgreSQL versions installed:

```sh
$ pg_config --version
```

```output
PostgreSQL 13.0 (Ubuntu 13.0-1.pgdg18.04+1)
```

```sh
$ /usr/lib/postgresql/11/bin/pg_config --version
```

```output
PostgreSQL 11.9 (Ubuntu 11.9-1.pgdg18.04+1)
```

In this case, you should be using `/usr/lib/postgresql/11/bin/pg_config`.

On CentOS, the correct path is `/usr/pgsql-11/bin/pg_config`.

## Using PostgreSQL extensions

### auto_explain

The [auto_explain](https://www.postgresql.org/docs/11/auto-explain.html) PostgreSQL module provides a means for logging execution plans of slow statements automatically, without having to run EXPLAIN by hand. This is especially helpful for tracking down un-optimized queries in large applications.

To enable the auto_explain extension, add `auto_explain` to `shared_preload_libraries` in the PostgreSQL server configuration parameters using the YB-TServer [--ysql_pg_conf_csv](../../../reference/configuration/yb-tserver/#ysql-pg-conf-csv) flag:

```sh
--ysql_pg_conf_csv="shared_preload_libraries=auto_explain"
```

Note that modifying `shared_preload_libraries` requires restarting the YB-TServer.

You can customize the following auto_explain parameters:

| Parameter | Description | Default |
| :--- | :--- | :--- |
| `log_min_duration` | Minimum statement execution time, in milliseconds, that will cause the statement's plan to be logged. Setting this to zero logs all plans. Minus-one (the default) disables logging. For example, if you set it to 250ms then all statements that run 250ms or longer will be logged. Only superusers can change this setting. | -1 |
| `log_analyze` | Print EXPLAIN ANALYZE output, rather than just EXPLAIN output when an execution plan is logged. When on, per-plan-node timing occurs for all statements executed, whether or not they run long enough to actually get logged. This can have an extremely negative impact on performance. Turning off `log_timing` ameliorates the performance cost, at the price of obtaining less information. Only superusers can change this setting. | false |
| `log_buffers` | Print buffer usage statistics when an execution plan is logged; equivalent to the BUFFERS option of EXPLAIN. Has no effect unless `log_analyze` is enabled. Only superusers can change this setting. | false |
| `log_timing` | Print per-node timing information when an execution plan is logged; equivalent to the TIMING option of EXPLAIN. The overhead of repeatedly reading the system clock can slow down queries significantly on some systems, so it may be beneficial to set this parameter to off when only actual row counts, and not exact times, are needed. Has no effect unless `log_analyze` is enabled. Only superusers can change this setting. | true |
| `log_triggers` | Include trigger execution statistics when an execution plan is logged. Has no effect unless `log_analyze` is enabled. Only superusers can change this setting. | false |
| `log_verbose` | Print verbose details when an execution plan is logged; equivalent to the VERBOSE option of EXPLAIN. Only superusers can change this setting. | false |
| `log_format` | The format of the EXPLAIN output. Allowed values are `text`, `xml`, `json`, and `yaml`. Only superusers can change this setting. | text |
| `log_nested_statements` | Consider nested statements (statements executed inside a function) for logging. When off, only top-level query plans are logged. Only superusers can change this setting. | false |
| `sample_rate` | Explain only a set fraction of the statements in each session. The default 1 means explain all the queries. In case of nested statements, either all will be explained or none. Only superusers can change this setting. | 1 |

Note that the default behavior is to do nothing, so you must set at least `auto_explain.log_min_duration` if you want any results.

#### auto_explain example

To change auto_explain parameters, use the SET statement. For example:

```sql
SET auto_explain.log_min_duration = 0;
SET auto_explain.log_analyze = true;
SELECT count(*)
    FROM pg_class, pg_index
    WHERE oid = indrelid AND indisunique;
```

This produces log output similar to the following in the PostgreSQL log file in the `tserver/logs` directory:

```output
LOG:  duration: 316.556 ms  plan:
        Query Text: SELECT count(*)
                   FROM pg_class, pg_index
                   WHERE oid = indrelid AND indisunique;
        Aggregate  (cost=216.39..216.40 rows=1 width=8) (actual time=316.489..316.489 rows=1 loops=1)
          ->  Nested Loop  (cost=0.00..213.89 rows=1000 width=0) (actual time=10.828..316.200 rows=110 loops=1)
                ->  Seq Scan on pg_index  (cost=0.00..100.00 rows=1000 width=4) (actual time=7.465..8.068 rows=110 loops=1)
                      Remote Filter: indisunique
                ->  Index Scan using pg_class_oid_index on pg_class  (cost=0.00..0.11 rows=1 width=4) (actual time=2.673..2.673 rows=1 loops=110)
                      Index Cond: (oid = pg_index.indrelid)
```

### file_fdw example

First, install the extension:

```sql
CREATE EXTENSION file_fdw;
```

Create a foreign server:

```sql
CREATE SERVER my_server FOREIGN DATA WRAPPER file_fdw;
```

Now, you can create foreign tables that access data from files. For example:

```sql
CREATE FOREIGN TABLE employees (id int, employee_name varchar) SERVER myserver OPTIONS (filename 'employees.csv', format 'csv');
```

You can execute SELECT statements on the foreign tables to access the data in the corresponding files.

### fuzzystrmatch example

```sql
CREATE EXTENSION fuzzystrmatch;

SELECT levenshtein('Yugabyte', 'yugabyte'), metaphone('yugabyte', 8);
```

```output
 levenshtein | metaphone
-------------+-----------
           2 | YKBT
(1 row)
```

### passwordcheck example

To enable the passwordcheck extension, add `passwordcheck` to `shared_preload_libraries` in the PostgreSQL server configuration parameters using the YB-TServer [`--ysql_pg_conf_csv`](../../../reference/configuration/yb-tserver/#ysql-pg-conf-csv) flag:

```sh
--ysql_pg_conf_csv="shared_preload_libraries=passwordcheck"
```

You can customize the following passwordcheck parameters:

| Parameter | Description | Default |
| :--- | :--- | :--- |
| minimum_length | Minimum password length. | 8 |
| maximum_length | Maximum password length. | 15 |
| restrict_lower | Passwords must include a lowercase character. | true |
| restrict_upper | Passwords must include an uppercase character. | true |
| restrict_numbers | Passwords must include a number. | true |
| restrict_special | Passwords must include a special character. | true |
| special_chars | The set of special characters. | <code>!@#$%^&*()_+{}\|\<\>?=</code> |

You can change passwordcheck parameters using a `SET` statement. For example, to increase the maximum length allowed and not require numbers, execute the following commands:

```sql
SET passwordcheck.maximum_length TO 20;
SET passwordcheck.restrict_numbers TO false;
```

When enabled, if a password is considered too weak, it's rejected with an error. For example:

```sql
yugabyte=# create role test_role password 'tooshrt';
```

```output
ERROR:  password is too short
```

```sql
yugabyte=# create role test_role password 'nonumbersinpassword';
```

```output
ERROR:  password must contain both letters and nonletters
```

```sql
yugabyte=# create role test_role password '123test_role123';
```

```output
ERROR:  password must not contain user name
```

The passwordcheck extension only works for passwords that are provided in plain text. For more information, refer to the [PostgreSQL passwordcheck documentation](https://www.postgresql.org/docs/11/passwordcheck.html).

### pgcrypto example

```sql
CREATE EXTENSION pgcrypto;
CREATE TABLE pgcrypto_example(id uuid PRIMARY KEY DEFAULT gen_random_uuid(), content text, digest text);
INSERT INTO pgcrypto_example (content, digest) values ('abc', digest('abc', 'sha1'));

SELECT * FROM pgcrypto_example;
```

```output
                  id                  | content |                   digest
--------------------------------------+---------+--------------------------------------------
 b8f2e2f7-0b8d-4d26-8902-fa4f5277869d | abc     | \xa9993e364706816aba3e25717850c26c9cd0d89d
(1 row)
```

### pg_stat_statements example

```sql
CREATE EXTENSION pg_stat_statements;

SELECT query, calls, total_time, min_time, max_time, mean_time, stddev_time, rows FROM pg_stat_statements;
```

To get the output of `pg_stat_statements` in JSON format, visit `https://<yb-tserver-ip>:13000/statements` in your web browser, where `<yb-tserver-ip>` is the IP address of any YB-TServer node in your cluster.

For more information on using pg_stat_statements in YugabyteDB, refer to [Get query statistics using pg_stat_statements](../../query-1-performance/pg-stat-statements/).

### spi example

YugabyteDB supports the following four (of five &mdash; `timetravel` is not currently supported) extensions provided in the `spi` module:

* `autoinc` functions auto-increment fields.
* `insert_username` functions track who changed a table.
* `moddatetime` functions track last modification times.
* `refint` functions implement referential integrity.

1. Set up a table with triggers for tracking modification time and user (role). Connect using `ysqlsh` and run the following commands:

    ```sql
    CREATE EXTENSION insert_username;
    CREATE EXTENSION moddatetime;

    CREATE TABLE spi_test (
      id int primary key,
      content text,
      username text not null,
      moddate timestamp DEFAULT CURRENT_TIMESTAMP NOT NULL
    );

    CREATE TRIGGER insert_usernames
      BEFORE INSERT OR UPDATE ON spi_test
      FOR EACH ROW
      EXECUTE PROCEDURE insert_username (username);

    CREATE TRIGGER update_moddatetime
      BEFORE UPDATE ON spi_test
      FOR EACH ROW
      EXECUTE PROCEDURE moddatetime (moddate);
    ```

1. Insert some rows. Each insert should add the current role as `username` and the current timestamp as `moddate`.

    ```sql
    SET ROLE yugabyte;
    INSERT INTO spi_test VALUES(1, 'desc1');

    SET ROLE postgres;
    INSERT INTO spi_test VALUES(2, 'desc2');
    INSERT INTO spi_test VALUES(3, 'desc3');

    SET ROLE yugabyte;
    INSERT INTO spi_test VALUES(4, 'desc4');

    SELECT * FROM spi_test ORDER BY id;
    ```

    ```output
     id | content | username |          moddate
    ----+---------+----------+----------------------------
      1 | desc1   | yugabyte | 2019-09-13 16:55:53.969907
      2 | desc2   | postgres | 2019-09-13 16:55:53.983306
      3 | desc3   | postgres | 2019-09-13 16:55:53.98658
      4 | desc4   | yugabyte | 2019-09-13 16:55:53.991315
    (4 rows)
    ```

    The `yugabyte` and (for compatibility) `postgres` YSQL users are created by default.

1. Update some rows. This should update both `username`  and `moddate` accordingly.

    ```sql
    UPDATE spi_test SET content = 'desc1_updated' WHERE id = 1;
    UPDATE spi_test SET content = 'desc3_updated' WHERE id = 3;

    SELECT * FROM spi_test ORDER BY id;
    ```

    ```output
    id |    content    | username |          moddate
    ----+---------------+----------+----------------------------
      1 | desc1_updated | yugabyte | 2019-09-13 16:56:27.623513
      2 | desc2         | postgres | 2019-09-13 16:55:53.983306
      3 | desc3_updated | yugabyte | 2019-09-13 16:56:27.634099
      4 | desc4         | yugabyte | 2019-09-13 16:55:53.991315
    (4 rows)
    ```

### tablefunc example

```sql
CREATE EXTENSION tablefunc;

CREATE TABLE t(k int primary key, v double precision);

PREPARE insert_k_v_pairs(int) AS
INSERT INTO t(k, v)
SELECT
  generate_series(1, $1),
  normal_rand($1, 1000.0, 10.0);
```

Test it as follows:

```sql
DELETE FROM t;

EXECUTE insert_k_v_pairs(10);

SELECT k, to_char(v, '9999.99') AS v
FROM t
ORDER BY k;
```

You'll see results similar to the following:

```output
 k  |    v
----+----------
  1 |   988.53
  2 |  1005.18
  3 |  1014.30
  4 |  1000.92
  5 |   999.51
  6 |  1000.94
  7 |  1007.45
  8 |   991.22
  9 |   987.95
 10 |   996.57
(10 rows)
```

Every time you repeat the test, you'll see different generated values for `v`.

For another example that uses `normal_rand()`, refer to [Analyzing a normal distribution with percent_rank(), cume_dist() and ntile()](../../../api/ysql/exprs/window_functions/analyzing-a-normal-distribution/). It populates a table with a large number (say 100,000) of rows and displays the outcome as a histogram that clearly shows the familiar bell-curve shape.

`tablefunc` also provides the `connectby()`, `crosstab()`, and `crosstabN()` functions.

The `connectby()` function displays a hierarchy of the kind that you see in an _"employees"_ table with a reflexive foreign key constraint where _"manager_id"_ refers to _"employee_id"_. Each next deeper level in the tree is indented from its parent following the well-known pattern.

The `crosstab()`and  `crosstabN()` functions produce "pivot" displays. The _"N"_ in crosstabN() indicates the fact that a few, `crosstab1()`, `crosstab2()`, `crosstab3()`, are provided natively by the extension and that you can follow documented steps to create more.

### postgres_fdw example

First, install the extension:

```sql
CREATE EXTENSION postgres_fdw;
```

To connect to a remote YSQL or PostgreSQL database, create a foreign server object. Specify the connection information (except the username and password) using the `OPTIONS` clause:

```sql
CREATE SERVER my_server FOREIGN DATA WRAPPER postgres_fdw OPTIONS (host 'host_ip', dbname 'external_db', port 'port_number');
```

Specify the username and password using `CREATE USER MAPPING`:

```sql
CREATE USER MAPPING FOR mylocaluser SERVER myserver OPTIONS (user 'remote_user', password 'password');
```

You can now create foreign tables using `CREATE FOREIGN TABLE` and `IMPORT FOREIGN SCHEMA`.

```sql
CREATE FOREIGN TABLE table_name (colname1 int, colname2 int) SERVER myserver OPTIONS (schema_name 'schema', table_name 'table');
IMPORT FOREIGN SCHEMA foreign_schema_name FROM SERVER my_server INTO local_schema_name;
```

You can execute SELECT statements on the foreign tables to access the data in the corresponding remote tables.

### postgresql-hll example

First, install `postgres-hll` [from source](https://github.com/citusdata/postgresql-hll#from-source) locally in a PostgreSQL instance. Use the same PostgreSQL version as that incorporated into YugabyteDB.

After you've installed the extension in PostgreSQL, copy the files to your YugabyteDB instance as follows:

```sh
$ cp -v "$(pg_config --pkglibdir)"/*hll*.so "$(yb_pg_config --pkglibdir)" &&
  cp -v "$(pg_config --sharedir)"/extension/*hll*.sql "$(yb_pg_config --sharedir)"/extension &&
  cp -v "$(pg_config --sharedir)"/extension/*hll*.control "$(yb_pg_config --sharedir)"/extension &&
  ./bin/ysqlsh -c "CREATE EXTENSION \"hll\";"
```

To run the example from the [postgresql-hll](https://github.com/citusdata/postgresql-hll#usage) repository, connect using `ysqlsh` and run the following:

```sql
yugabyte=# CREATE TABLE helloworld (id integer, set hll);
CREATE TABLE
--- Insert an empty HLL
yugabyte=# INSERT INTO helloworld(id, set) VALUES (1, hll_empty());
INSERT 0 1
--- Add a hashed integer to the HLL
yugabyte=# UPDATE helloworld SET set = hll_add(set, hll_hash_integer(12345)) WHERE id = 1;
UPDATE 1
--- Or add a hashed string to the HLL
yugabyte=# UPDATE helloworld SET set = hll_add(set, hll_hash_text('hello world')) WHERE id = 1;
UPDATE 1
--- Get the cardinality of the HLL
yugabyte=# SELECT hll_cardinality(set) FROM helloworld WHERE id = 1;
```

```output
 hll_cardinality
-----------------
               2
(1 row)
```

### uuid-ossp example

First, install the extension:

```sql
CREATE EXTENSION uuid-ossp;
```

Connect using `ysqlsh` and run the following:

```sql
SELECT uuid_generate_v1(), uuid_generate_v4(), uuid_nil();
```

```output
           uuid_generate_v1           |           uuid_generate_v4           |               uuid_nil
--------------------------------------+--------------------------------------+--------------------------------------
 69975ce4-d827-11e9-b860-bf2e5a7e1380 | 088a9b6c-46d8-4276-852b-64908b06a503 | 00000000-0000-0000-0000-000000000000
(1 row)
```

### PostGIS example

**YSQL does not yet support GiST indexes**. This is tracked in [GitHub issue #1337](https://github.com/yugabyte/yugabyte-db/issues/1337).

#### Install PostGIS

##### macOS

There are two ways to install PostGIS on macOS:

* Download and install [Postgres.app](https://postgresapp.com/)

* Or, install with Homebrew:

    ```sh
    $ brew install postgres postgis
    ```

##### Ubuntu

Add the [PostgreSQL APT sources](https://www.postgresql.org/download/linux/ubuntu/). Then, use `apt` to install:

```shell script
sudo apt-get install postgresql-11 postgresql-11-postgis-3
```

##### CentOS

Get the YUM repository from the [PostgreSQL website](https://www.postgresql.org/download/linux/redhat/). Then, use `yum` or `dnf` to install:

```sh
sudo yum install postgresql11-server postgis31_11 postgis31_11-client
```

#### Install the extension

Copy the extension files to your YugabyteDB installation as follows:

```sh
$ cp -v "$(pg_config --pkglibdir)"/*postgis*.so "$(yb_pg_config --pkglibdir)" &&
  cp -v "$(pg_config --sharedir)"/extension/*postgis*.sql "$(yb_pg_config --sharedir)"/extension &&
  cp -v "$(pg_config --sharedir)"/extension/*postgis*.control "$(yb_pg_config --sharedir)"/extension
```

On Linux systems, PostGIS libraries have dependencies that must also be installed. Use the extensions option of the post-install tool, available in YugabyteDB 2.3.2 and later, as follows:

```sh
./bin/post_install.sh -e
```

Then, create the extension:

```sh
./bin/ysqlsh -c "CREATE EXTENSION postgis;"
```

This may take a couple of minutes.

#### Example

1. Get a sample [PostGIS dataset](https://data.edmonton.ca/Geospatial-Boundaries/City-of-Edmonton-Neighbourhood-Boundaries/jfvj-x253):

    ```sh
    $ wget -O edmonton.zip "https://data.edmonton.ca/api/geospatial/jfvj-x253?method=export&format=Shapefile" && unzip edmonton.zip
    ```

1. Extract the dataset using the `shp2pgsql` tool. This should come with your PostgreSQL installation — it is not yet packaged with YSQL.

    ```sh
    $ shp2pgsql geo_export_*.shp > edmonton.sql
    ```

1. Edit the generated `edmonton.sql` for YSQL compatibility.

    * First, inline the `PRIMARY KEY` declaration for `gid` as YSQL does not yet support adding primary key constraints after the table creation.
    * Additionally, for simplicity, change the table name (and references to it in the associated `INSERT` statements) to just `geo_export` (in other words, remove the UUID postfix).

    The `edmonton.sql` file should now start as follows:

    ```sql
    SET CLIENT_ENCODING TO UTF8;
    SET STANDARD_CONFORMING_STRINGS TO ON;
    BEGIN;
    CREATE TABLE "geo_export" (gid serial PRIMARY KEY,
      "area_km2" numeric,
      "name" varchar(254),
      "number" numeric);
    SELECT AddGeometryColumn('','geo_export','geom','0','MULTIPOLYGON',2);

    INSERT INTO "geo_export" ("area_km2","name","number",geom) VALUES ...
    ```

1. Load the sample data.

    ```sh
    $ ./bin/ysqlsh -a -f edmonton.sql
    ```

1. Run some sample queries. Connect using `ysqlsh` and run the following:

    ```sql
    SELECT name, area_km2, ST_Area(geom), ST_Area(geom)/area_km2 AS area_ratio FROM "geo_export" LIMIT 10;
    ```

    ```output
                name            |     area_km2      |       st_area        |      area_ratio
    ----------------------------+-------------------+----------------------+----------------------
    River Valley Terwillegar   | 3.077820277027079 | 0.000416617423004673 | 0.000135361192501822
    Carleton Square Industrial | 0.410191631391664 | 5.56435079305678e-05 | 0.000135652469899947
    Cy Becker                  | 1.015144841249301 | 0.000137900847258255 | 0.000135843518732308
    Elsinore                   | 0.841471068786406 | 0.000114331091817771 |  0.00013587049639468
    McLeod                     | 0.966538217483227 | 0.000131230296771637 | 0.000135773520796051
    Gainer Industrial          | 0.342464541730177 | 4.63954326887451e-05 | 0.000135475142782225
    Coronet Industrial         | 1.606907195063447 | 0.000217576340986435 | 0.000135400688760899
    Marquis                    | 9.979100854886905 |  0.00135608901739072 | 0.000135892906295924
    South Terwillegar          | 1.742840325820606 | 0.000235695089933611 | 0.000135236192576985
    Carlisle                   | 0.961897333826841 | 0.000130580966739925 | 0.000135753538499185
    (10 rows)
    ```

    ```sql
    SELECT a.name, b.name FROM "geo_export" AS a, "geo_export" AS b
    WHERE ST_Intersects(a.geom, b.geom) AND a.name LIKE 'University of Alberta';
    ```

    ```output
            name          |          name
    -----------------------+-------------------------
    University of Alberta | University of Alberta
    University of Alberta | McKernan
    University of Alberta | Belgravia
    University of Alberta | Garneau
    University of Alberta | River Valley Mayfair
    University of Alberta | River Valley Walterdale
    University of Alberta | Windsor Park
    (7 rows)
    ```

### pgsql-postal example

#### Installation

First install `libpostal` [from source](https://github.com/openvenues/libpostal) locally:

```sh
`make -j$(nproc) && sudo make install`
```

To build `pgsql-postal` against the correct PostgreSQL version for YugabyteDB compatibility, install PostgreSQL 11 on your system as described in the [PostGIS example](#postgis-example).

Build `pgsql-postal` [from source](https://github.com/pramsey/pgsql-postal) locally. First make sure to set `PG_CONFIG` in `Makefile` to the correct PostgreSQL version (for example, on CentOS `PG_CONFIG=/usr/pgsql-11/bin/pg_config`), then run `make`.

Copy the needed files into your YugabyteDB installation:

```sh
$ cp -v /usr/local/lib/libpostal.so* "$(yb_pg_config --pkglibdir)" &&
  cp -v postal-1.0.sql postal.control "$(yb_pg_config --sharedir)"/extension
```

On Linux systems, run the post-install tool:

```sh
./bin/post_install.sh -e
```

Create the extension:

```sh
./bin/ysqlsh -c "CREATE EXTENSION postal"
```

#### Example

Run some sample queries by connecting using `ysqlsh` and running the following:

```sql
SELECT unnest(postal_normalize('412 first ave, victoria, bc'));
```

```output
                  unnest
------------------------------------------
 412 1st avenue victoria british columbia
 412 1st avenue victoria bc
 412 1 avenue victoria british columbia
 412 1 avenue victoria bc
(4 rows)
```

```sql
SELECT postal_parse('412 first ave, victoria, bc');
```

```output
                                  postal_parse
---------------------------------------------------------------------------------
 {"city": "victoria", "road": "first ave", "state": "bc", "house_number": "412"}
(1 row)
```
