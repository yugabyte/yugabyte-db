![pg11-test](https://github.com/percona/pg_stat_monitor/workflows/pg11-test/badge.svg)
![pg12-test](https://github.com/percona/pg_stat_monitor/workflows/pg12-test/badge.svg)
![pg13-test](https://github.com/percona/pg_stat_monitor/workflows/pg13-test/badge.svg)

## What is pg_stat_monitor?
The **pg_stat_monitor** is a **PostgreSQL Query Performance Monitoring** tool, based on PostgreSQL's contrib module ``pg_stat_statements``. PostgreSQL’s pg_stat_statements provides the basic statistics, which is sometimes not enough. The major shortcoming in pg_stat_statements is that it accumulates all the queries and their statistics and does not provide aggregated statistics nor histogram information. In this case, a user needs to calculate the aggregate which is quite expensive. 

**pg_stat_monitor** is developed on the basis of pg_stat_statements as its more advanced replacement. It provides all the features of pg_stat_statements plus its own feature set.  

### How pg_stat_monitor works?

pg_stat_monitor accumulates the information in the form of buckets. All the aggregated information is bucket based. The size of a bucket and the number of buckets should be configured using GUC (Grand Unified Configuration). When a bucket time elapses, pg_stat_monitor resets all the statistics and switches to the next bucket. After the last bucket elapses, pg_stat_monitor goes back to the first bucket. All the data on the first bucket will vanish; therefore, users must read the buckets before that to not lose the data.

## Documentation
1. [Supported PostgreSQL Versions](#supported-postgresql-versions)
2. [Installation](#installation)
3. [Setup](#setup) 
4. [User Guide](https://github.com/percona/pg_stat_monitor/blob/master/docs/USER_GUIDE.md)
6. [Release Notes](https://github.com/percona/pg_stat_monitor/blob/master/docs/RELEASE_NOTES.md)
7. [License](https://github.com/percona/pg_stat_monitor/blob/master/LICENSE)
8. [Submitting Bug Reports](#submitting-bug-reports)
9. [Copyright Notice](#copyright-notice)

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
``pg_stat_monitor`` is supplied as part of Percona Distribution for PostgreSQL. The rpm/deb packages are available from Percona repositories. Refer to [Percona Documentation](https://www.percona.com/doc/postgresql/LATEST/installing.html) for installation instructions. 

### Installing from PGXN

You can install ``pg_stat_monitor`` from PGXN (PostgreSQL Extensions Network) using the [PGXN client](https://pgxn.github.io/pgxnclient/). 


Use the following command:

```sh
pgxn install pg_stat_monitor
```

### Installing from source code

You can download the source code of the latest release of ``pg_stat_monitor``  from [this GitHub page](https://github.com/Percona/pg_stat_monitor/releases) or using git:
```sh
git clone git://github.com/Percona/pg_stat_monitor.git
```

Compile and install the extension
```sh
cd pg_stat_monitor
make USE_PGXS=1
make USE_PGXS=1 install
```

## Setup
``pg_stat_monitor`` cannot be enabled in your running PostgreSQL instance. ``pg_stat_monitor`` needs to be loaded at the start time. This requires adding the  ``pg_stat_monitor`` extension for the ``shared_preload_libraries`` parameter and restarting the PostgreSQL instance.

You can set the  ``pg_stat_monitor`` extension in the ``postgresql.conf`` file.

```
# - Shared Library Preloading -

shared_preload_libraries = 'pg_stat_monitor' # (change requires restart)
#local_preload_libraries = ''
#session_preload_libraries = ''
```

Or you can set it from `psql` terminal using the ``alter system`` command.

```sql
ALTER SYSTEM SET shared_preload_libraries = 'pg_stat_monitor';
ALTER SYSTEM
```

```sh
sudo systemctl restart postgresql-13
```


Create the extension using the ``CREATE EXTENSION`` command.
```sql
CREATE EXTENSION pg_stat_monitor;
CREATE EXTENSION
```

```sql
-- Select some of the query information, like client_ip, username and application_name etc.

postgres=# SELECT application_name, userid AS user_name, datname AS database_name, substr(query,0, 50) AS query, calls, client_ip 
           FROM pg_stat_monitor;
 application_name | user_name | database_name |                       query                       | calls | client_ip 
------------------+-----------+---------------+---------------------------------------------------+-------+-----------
 psql             | vagrant   | postgres      | SELECT application_name, userid::regrole AS user_ |     1 | 127.0.0.1
 psql             | vagrant   | postgres      | SELECT application_name, userid AS user_name, dat |     3 | 127.0.0.1
 psql             | vagrant   | postgres      | SELECT application_name, userid AS user_name, dat |     1 | 127.0.0.1
 psql             | vagrant   | postgres      | SELECT application_name, userid AS user_name, dat |     8 | 127.0.0.1
 psql             | vagrant   | postgres      | SELECT bucket, substr(query,$1, $2) AS query, cmd |     1 | 127.0.0.1
(5 rows)


```

```sql
-- Select queries along with elevel, message and sqlcode which have some errors.

SELECT  decode_error_level(elevel) AS elevel, sqlcode, query, message FROM pg_stat_monitor WHERE elevel != 0;
 elevel.   | sqlcode |                                           query                                           |                    message                     
--------------------+---------+-------------------------------------------------------------------------------------------+------------------------------------------------
 ERROR     |     132 | select count(*) from pgbench_branches                                                     | permission denied for table pgbench_branches
 ERROR     |     130 | select 1/0;                                                                               | division by zero
 ERROR     |     132 | SELECT decode_elevel(elevel), sqlcode, message from pg_stat_monitor where elevel != 0;    | function decode_elevel(integer) does not exist
 ERROR     |     132 | drop table if exists pgbench_accounts, pgbench_branches, pgbench_history, pgbench_tellers | must be owner of table pgbench_accounts
(4 rows)

```

To learn more about ``pg_stat_monitor`` configuration and usage, see [User Guide](https://github.com/percona/pg_stat_monitor/blob/master/docs/USER_GUIDE.md).

## Submitting Bug Reports

If you found a bug in ``pg_stat_monitor``, please submit the report to the [Jira issue tracker](https://jira.percona.com/projects/PG/issues)

Start by searching the open tickets for a similar report. If you find that someone else has already reported your issue, then you can upvote that report to increase its visibility.

If there is no existing report, submit your report following these steps:

Sign in to [Jira issue tracker](https://jira.percona.com/projects/PG/issues). You will need to create an account if you do not have one.

In the *Summary*, *Description*, *Steps To Reproduce*, *Affects Version* fields describe the problem you have detected. 

As a general rule of thumb, try to create bug reports that are:

- Reproducible: describe the steps to reproduce the problem.

- Unique: check if there already exists a JIRA ticket to describe the problem.

- Scoped to a Single Bug: only report one bug in one JIRA ticket.


## Copyright Notice

Portions Copyright © 2018-2020, Percona LLC and/or its affiliates

Portions Copyright (c) 1996-2020, PostgreSQL Global Development Group

Portions Copyright (c) 1994, The Regents of the University of California
