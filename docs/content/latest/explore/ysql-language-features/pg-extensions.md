---
title: PostgreSQL extensions
headerTitle: PostgreSQL extensions
linkTitle: PostgreSQL extensions
description: Summary of supported PostgreSQL extensions
image: /images/section_icons/secure/create-roles.png
summary: Reference for YSQL extensions
menu:
  latest:
    identifier: explore-ysql-postgresql-extensions
    parent: explore-ysql-language-features
    weight: 4400
aliases:
  - /latest/explore/ysql-language-features/advanced-features/extensions/
  - /latest/api/ysql/extensions/
isTocNested: true
showAsideToc: true
---

PostgreSQL extensions provide a way to extend the functionality of a database by bundling SQL objects into a package and using them as a unit.
This page describes the PostgreSQL extensions supported by YugabyteDB.

## PostgreSQL extensions supported by YugabyteDB

Extensions are either pre-bundled with YugabyteDB, or require installation:

* **Pre-bundled** extensions are included in the standard YugabyteDB distribution and can be enabled in YSQL by running the [CREATE EXTENSION](../../../api/ysql/the-sql-language/statements/ddl_create_extension/) statement.
* **Requires installation** - you must install these extensions manually before you can enable them using CREATE EXTENSION. Refer to [Installing an extension](#installing-extensions).

For information about using a specific extension in YugabyteDB, follow the More Information links in the following table.

| Extension | Status | Description | More information |
| :-------- | :----- | :---------- | :------ |
**PostgreSQL modules**
[fuzzystrmatch module](https://www.postgresql.org/docs/11/fuzzystrmatch.html) | Pre-bundled | Provides several functions to determine similarities and distance between strings. | [fuzzystrmatch example](#fuzzystrmatch-example) |
[pgcrypto module](https://www.postgresql.org/docs/11/pgcrypto.html)| Pre-bundled | Provides various cryptographic functions. | [pgcrypto example](#pgcrypto-example) |
[pg_stat_statements module](https://www.postgresql.org/docs/11/pgstatstatements.html)| Pre-bundled| Provides a means for tracking execution statistics of all SQL statements executed by a server. | [pg_stat_statements example](#pg-stat-statements-example)
[spi module](https://www.postgresql.org/docs/11/contrib-spi.html)|Pre-bundled | Lets you use the Server Programming Interface (SPI) to create user-defined functions and stored procedures in C, and to run YSQL queries directly against YugabyteDB. | [spi example](#spi-example)
[hstore module](https://www.postgresql.org/docs/11/hstore.html) | Pre-bundled | Implements the hstore data type for storing sets of key/value pairs in a single PostgreSQL value. | |
[pg_trgm module](https://www.postgresql.org/docs/11/pgtrgm.html) | Pre-bundled | Provides functions and operators for determining the similarity of alphanumeric text based on trigram matching, as well as index operator classes that support fast searching for similar strings. | |
[postgres_fdw module](https://www.postgresql.org/docs/11/postgres-fdw.html) | Pre-bundled | Provides the foreign-data wrapper postgres_fdw, which can be used to access data stored in external PostgreSQL servers. |
[file_fdw module](https://www.postgresql.org/docs/11/file-fdw.html) | Pre-bundled | Provides the foreign-data wrapper file_fdw, which can be used to access data files in the server's file system. |
[sslinfo module](https://www.postgresql.org/docs/11/sslinfo.html) | Pre-bundled | Provides information about the SSL certificate that the current client provided when connecting to PostgreSQL. |
[tablefunc module](https://www.postgresql.org/docs/11/tablefunc.html) | Pre-bundled | Provides several table functions. For example, `normal_rand()` creates values, picked using a pseudorandom generator, from an ideal normal distribution. You specify how many values you want, and the mean and standard deviation of the ideal distribution. You use it in the same way that you use `generate_series()` | [tablefunc example](#tablefunc-example)
[uuid-ossp module](https://www.postgresql.org/docs/11/uuid-ossp.html) | Requires installation | Provides functions to generate universally unique identifiers (UUIDs), and functions to produce certain special UUID constants. | [uuid-ossp example](#uuid-ossp-example) |
| **Other** ||||
[pg_hint_plan](https://pghintplan.osdn.jp/pg_hint_plan.html) | Pre-bundled | Tweak execution plans using "hints", which are descriptions in the form of SQL comments. | [pg_hint_plan example](../../query-1-performance/pg-hint-plan/#root) |
[PGAudit](https://www.pgaudit.org/) | Pre-bundled | The PostgreSQL Audit Extension (pgAudit) provides detailed session and/or object audit logging via the standard PostgreSQL logging facility. |
[pg_stat_monitor](https://github.com/percona/pg_stat_monitor) | Pre-bundled | A PostgreSQL query performance monitoring tool, based on PostgreSQL's contrib module pg_stat_statements. |
[Orafce](https://github.com/orafce/orafce)| Pre-bundled | Provides compatibility with Oracle functions and packages that are either missing or implemented differently in YugabyteDB and PostgreSQL. This compatibility layer can help you port your Oracle applications to YugabyteDB. | |
[PostGIS](https://postgis.net/) | Requires installation | A spatial database extender for PostgreSQL-compatible object-relational databases. | [PostGIS example](#postgis-example)|
[postgresql-hll](https://github.com/citusdata/postgresql-hll) | Requires installation | Introduces the data type `hll`, which is a HyperLogLog data structure. | [postgresql-hll-example](#postgresql-hll-example) |
[YCQL_fdw](https://github.com/YugaByte/yugabyte-db/issues/830) | In-progress | Access YCQL tables via the YSQL API. | |
[pg_cron](https://github.com/citusdata/pg_cron) | In-progress | cron-based job scheduler for PostgreSQL that runs inside the database as an extension. It uses the same syntax as regular cron, but it allows you to schedule PostgreSQL commands directly from the database. | |
[PostgreSQL Anonymizer](https://postgresql-anonymizer.readthedocs.io/en/latest/) | In-progress | Mask or replace personally identifiable information (PII) or commercially sensitive data from a PostgreSQL database. | |
[PG Partition Manager](https://github.com/pgpartman/pg_partman) | In-progress | Create and manage both time-based and serial-based table partition sets. | |

## Installing extensions

If an extension is not pre-bundled, you need to install it manually before you can enable it using the [CREATE EXTENSION](../../../api/ysql/the-sql-language/statements/ddl_create_extension/) statement. You can install only extensions that are supported by YugabyteDB.

Ideally, use the same version of the PostgreSQL extension as that used by YugabyteDB. To see the version of PostgreSQL used in your YugabyteDB installation, enter the following `ysqlsh` command:

```sh
$ ./bin/ysqlsh --version
```

```output
psql (PostgreSQL) 11.2-YB-2.11.2.0-b0
```

Currently, in a multi-node setup, you need to install the extension on _every_ node in the cluster.

In a read replica setup, install extensions on the primary instance, not on the read replica. Once installed, the extension replicates to the read replica.

You cannot install new extensions in Yugabyte Cloud. If you need a database extension that is not pre-bundled with YugabyteDB added to a Yugabyte Cloud cluster, contact [Yugabyte Support](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431) with the names of the cluster and extension, or [reach out on Slack](https://yugabyte-db.slack.com/).

### Install an extension

Typically, extensions need three types of files:

* Shared library files (`<name>.so`)
* SQL files (`<name>--<version>.sql`)
* Control files (`<name>.control`)

To install an extension, you need to copy these files into the respective directories of your YugabyteDB installation.

Shared library files will be in the `pkglibdir` directory, while SQL and control files should be in the `extension` subdirectory of the `libdir` directory.

To find these directories on your local installation, use the YugabyteDB `pg_config` executable.

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

To get these files for your target extension, build the extension from scratch following the extension's build instructions.

Alternatively, if you already have PostgreSQL (use version `11.2` for best YSQL compatibility) with the extension installed, you can find these files as follows:

```sh
$ ls "$(pg_config --pkglibdir)" | grep <name>
```

```sh
$ ls "$(pg_config --sharedir)"/extension/ | grep <name>
```

Copy the files to the YugabyteDB installation, and restart the cluster (or the respective node in a multi-node install).

Finally, connect to the cluster using `ysqlsh` and run the `CREATE EXTENSION` statement to create the extension.

## Using PostgreSQL extensions

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

Every time you repeat the test, you'll see different generated values for `v`. The section [Analyzing a normal distribution with percent_rank(), cume_dist() and ntile()](https://github.com/yugabyte/yugabyte-db/tree/master/docs/content/latest/api/ysql/exprs/window_functions/analyzing-a-normal-distribution/), in the major documentation section [Window functions](https://github.com/yugabyte/yugabyte-db/tree/master/docs/content/latest/api/ysql/exprs/window_functions), uses `normal_rand()`. It populates a table with a large number (say 100,000) of rows and displays the outcome as a histogram that clearly shows the familiar bell-curve shape.

These other functions are brought by `tablefunc`: `connectby()`; and `crosstab()` and `crosstabN()`.

The `connectby()` function displays a hierarchy of the kind that you see in an _"employees"_ table with a reflexive foreign key constraint where _"manager_id"_ refers to _"employee_id"_. Each next deeper level in the tree is indented from its parent following the well-known pattern.

The `crosstab()`and  `crosstabN()` functions produce "pivot" displays. The _"N"_ in crosstabN() indicates the fact that a few, `crosstab1()`, `crosstab2()`, `crosstab3()`, are provided natively by the extension and that you can follow documented steps to create more.

### postgresql-hll example

First, install `postgres-hll` [from source](https://github.com/citusdata/postgresql-hll#from-source) locally in a PostgreSQL instance.

Ideally, use the same PostgreSQL version as that incorporated into YugabyteDB. You can see the PostgreSQL version incorporated in a YugabyteDB installation by using the following `ysqlsh` command:

```sh
$ ./bin/ysqlsh --version
psql (PostgreSQL) 11.2-YB-2.1.2.0-b0
```

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

The easiest way to install the extension is to copy the files from an existing PostgreSQL installation into Yugabyte, and then create the extension.

```sh
$ cp -v "$(pg_config --pkglibdir)"/*uuid-ossp*.so "$(yb_pg_config --pkglibdir)" && 
  cp -v "$(pg_config --sharedir)"/extension/*uuid-ossp*.sql "$(yb_pg_config --sharedir)"/extension && 
  cp -v "$(pg_config --sharedir)"/extension/*uuid-ossp*.control "$(yb_pg_config --sharedir)"/extension &&
  ./bin/ysqlsh -c "CREATE EXTENSION \"uuid-ossp\";"
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

{{< note title="Note" >}}

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

{{< /note >}}

#### Install the extension

Follow the instructions in [Installing extensions](#installing-extensions) to copy the extension files to your YugabyteDB installation.

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
