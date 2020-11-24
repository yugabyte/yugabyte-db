## What is pg_stat_monitor?
The pg_stat_monitor is the statistics collection tool based on PostgreSQL's contrib module ``pg_stat_statements``. PostgreSQL’s pg_stat_statements provides the basic statistics, which is sometimes not enough. The major shortcoming in pg_stat_statements is that it accumulates all the queries and their statistics and does not provide aggregated statistics nor histogram information. In this case, a user needs to calculate the aggregate which is quite expensive. ``pg_stat_monitor`` is developed on the basis of pg_stat_statements as its more advanced replacement. It provides all the features of pg_stat_statements plus its own feature set.  

## Documentation
1. [Supported PostgreSQL Versions](#supported-postgresql-versions)
2. [Installation](#installation)
3. [Setup](#setup) 
4. [User Guide](https://github.com/percona/pg_stat_monitor/blob/master/docs/USER_GUIDE.md)
6. [Release Notes](https://github.com/percona/pg_stat_monitor/blob/master/docs/RELEASE_NOTES.md)
7. [License](https://github.com/percona/pg_stat_monitor/blob/master/LICENSE)
8. [Copyright Notice](#copyright-notice)

## Supported PostgreSQL Versions
The ``pg_stat_monitor`` should work on the latest version of PostgreSQL but is only tested with these PostgreSQL versions:

| Distribution            |  Version       | Supported          |
| ------------------------|----------------|--------------------|
| PostgreSQL              | Version < 11   | :x:                |
| PostgreSQL              | Version 11     | :heavy_check_mark: |
| PostgreSQL              | Version 12     | :heavy_check_mark: |
| PostgreSQL              | Version 13     | :heavy_check_mark: |
| Percona Distribution    | Version < 11   | :x:                |
| Percona Distribution    | Version 11     | :heavy_check_mark: |
| Percona Distribution    | Version 12     | :heavy_check_mark: |
| Percona Distribution    | Version 13     | :heavy_check_mark: |

## Installation
pg_stat_monitor is supplied as part of Percona Distribution for PostgreSQL. The rpm/deb packages are available from Percona repositories. Refer to [Percona Documentation](https://www.percona.com/doc/postgresql/LATEST/installing.html) for installation instructions. 

The source code of latest release of ``pg_stat_monitor`` can be downloaded from [this GitHub page](https://github.com/Percona/pg_stat_monitor/releases) or it can be downloaded using the git:
```sh
git clone git://github.com/Percona/pg_stat_monitor.git
```

Compile and Install the extension
```sh
cd pg_stat_monitor
make USE_PGXS=1
make USE_PGXS=1 install
```

## Setup
``pg_stat_monitor`` cannot be installed in your running PostgreSQL instance. It should be set in the ``postgresql.conf`` file.

```
# - Shared Library Preloading -

shared_preload_libraries = 'pg_stat_monitor' # (change requires restart)
#local_preload_libraries = ''
#session_preload_libraries = ''
```

Or you can do from `psql` terminal using the ``alter system`` command.

``pg_stat_monitor`` needs to be loaded at the start time. This requires adding the  ``pg_stat_monitor`` extension for the ``shared_preload_libraries`` parameter and restart the PostgreSQL instance.
```sql
postgres=# ALTER SYSTEM SET shared_preload_libraries = 'pg_stat_monitor';
ALTER SYSTEM

sudo systemctl restart postgresql-13
```


Create the extension using the ``CREATE EXTENSION`` command.
```sql
CREATE EXTENSION pg_stat_monitor;
CREATE EXTENSION
```

```sql
SELECT application_name, userid::regrole AS user_name, datname AS database_name, substr(query,0, 50) AS query, calls, client_ip 
           FROM pg_stat_monitor, pg_database 
           WHERE dbid = oid;
 
 application_name  | user_name | database_name |                       query                       | calls | client_ip 
-------------------+-----------+---------------+---------------------------------------------------+-------+-----------
 psql              | vagrant   | postgres      | SELECT elevel, sqlcode, message from pg_stat_moni |     1 | 127.0.0.1
 psql              | vagrant   | postgres      | SELECT c.relchecks, c.relkind, c.relhasindex, c.r |     1 | 127.0.0.1
 pg_cron scheduler | vagrant   | postgres      | update cron.job_run_details set status = $1, retu |     1 | 127.0.0.1
 pgbench           | vagrant   | postgres      | vacuum analyze pgbench_accounts                   |     1 | 10.0.2.15
 pgbench           | vagrant   | postgres      | alter table pgbench_branches add primary key (bid |     1 | 10.0.2.15
 psql              | vagrant   | postgres      | SELECT pg_catalog.quote_ident(c.relname) FROM pg_ |     1 | 127.0.0.1
 psql              | vagrant   | postgres      | SELECT decode_error_level(elevel), sqlcode, messa |     2 | 127.0.0.1
(37 rows)
```

```sql
SELECT decode_error_level(elevel) AS elevel, sqlcode, message 
           FROM pg_stat_monitor
           WHERE elevel != 0;
 
 elevel.            | sqlcode |                    message                     
--------------------+---------+------------------------------------------------
 ERROR              |     132 | permission denied for table pgbench_branches
 ERROR              |     130 | division by zero
 ERROR              |     132 | function decode_elevel(integer) does not exist
 ERROR              |     132 | must be owner of table pgbench_accounts
(4 rows)
```

## Copyright Notice
Copyright (c) 2006 - 2020, Percona LLC.
