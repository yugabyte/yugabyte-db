![pg11-test](https://github.com/percona/pg_stat_monitor/workflows/pg11-test/badge.svg)
![pg12-test](https://github.com/percona/pg_stat_monitor/workflows/pg12-test/badge.svg)
![pg13-test](https://github.com/percona/pg_stat_monitor/workflows/pg13-test/badge.svg)
![pg14-test](https://github.com/percona/pg_stat_monitor/workflows/pg14-test/badge.svg)

[![Coverage Status](https://coveralls.io/repos/github/percona/pg_stat_monitor/badge.svg)](https://coveralls.io/github/percona/pg_stat_monitor)

## What is pg_stat_monitor?

**pg_stat_monitor** is a **Query Performance Monitoring** tool for [Percona Distribution for PostgreSQL](https://www.percona.com/software/postgresql-distribution) and PostgreSQL. **pg_stat_monitor** is based on PostgreSQL's contrib module ``pg_stat_statements``. ``pg_stat_statements`` provides the basic statistics, which is sometimes not enough. The major shortcoming in ``pg_stat_statements`` is that it accumulates all the queries and their statistics and does not provide aggregated statistics nor histogram information. In this case, a user would need to calculate the aggregates, which is quite an expensive operation. 

**pg_stat_monitor** is developed on the basis of pg_stat_statements as is a more advanced replacement. It provides all the features of pg_stat_statements plus its own feature set.  

### How does pg_stat_monitor work?

``pg_stat_monitor`` accumulates the information in the form of buckets. All the aggregated information is bucket based. The size of a bucket and the number of buckets should be configured using GUC (Grand Unified Configuration). When a bucket time elapses, ``pg_stat_monitor`` resets all the statistics and switches to the next bucket. After the last bucket elapses, ``pg_stat_monitor`` goes back to the first bucket. All the data on the first bucket is cleared out with new writes; therefore, to not lose the data, users must read the buckets before that.

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
``pg_stat_monitor`` should work on the latest version of both [Percona Distribution for PostgreSQL](https://www.percona.com/software/postgresql-distribution) and PostgreSQL and is currently tested against thes following versions:

| Distribution                        | Version | Supported          |
| ------------------------------------|---------|--------------------|
| PostgreSQL                          |  < 11   | :x:                |
| PostgreSQL                          |  11     | :heavy_check_mark: |
| PostgreSQL                          |  12     | :heavy_check_mark: |
| PostgreSQL                          |  13     | :heavy_check_mark: |
| Percona Distribution for PostgreSQL |  < 11   | :x:                |
| [Percona Distribution for PostgreSQL](https://www.percona.com/downloads/percona-postgresql-11/)               |  11     | :heavy_check_mark: |
| [Percona Distribution for PostgreSQL](https://www.percona.com/downloads/percona-postgresql-12/)               |  12     | :heavy_check_mark: |
| [Percona Distribution for PostgreSQL](https://www.percona.com/downloads/percona-postgresql-13/)               |  13     | :heavy_check_mark: |

## Installation

You can install ``pg_stat_monitor`` from [Percona repositories](#installing-from-percona-repositories) and from [source code](#installing-from-source-code).

### Installing from Percona repositories

``pg_stat_monitor`` is supplied as part of Percona Distribution for PostgreSQL. The RPM/DEB packages are available from Percona's repositories. To install ``pg_stat_monitor``, follow [the installation instructions](https://www.percona.com/doc/postgresql/LATEST/installing.html). 

### Installing from PGXN

You can install ``pg_stat_monitor`` from PGXN (PostgreSQL Extensions Network) using the [PGXN client](https://pgxn.github.io/pgxnclient/). 


Use the following command:

```sh
pgxn install pg_stat_monitor
```

### Installing from source code

You can download the source code of the latest release of ``pg_stat_monitor``  from [the releases page on GitHub](https://github.com/Percona/pg_stat_monitor/releases) or using git:
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
``pg_stat_monitor`` cannot be enabled in your running ``postgresql`` instance, it needs to be loaded at the start time. This requires adding the ``pg_stat_monitor`` extension to the ``shared_preload_libraries`` parameter and restarting the ``postgresql`` instance.

You can set the ``pg_stat_monitor`` extension in the ``postgresql.conf`` file.

```
# - Shared Library Preloading -

shared_preload_libraries = 'pg_stat_monitor' # (change requires restart)
#local_preload_libraries = ''
#session_preload_libraries = ''
```

Or you can set it from `psql` terminal using the ``ALTER SYSTEM`` command.

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

To learn more about ``pg_stat_monitor`` configuration and usage, see the [User Guide](https://github.com/percona/pg_stat_monitor/blob/master/docs/USER_GUIDE.md).

## Submitting Feature Requests or Bug Reports

If you would like to request a feature of if you found a bug in ``pg_stat_monitor``, please submit the report to the [Jira issue tracker](https://jira.percona.com/projects/PG/issues).

Start by searching the open tickets for a similar report. If you find that someone else has already reported your issue, then you can upvote that report to increase its visibility.

If there is no existing report, submit your report following these steps:

1. Sign in to [Jira issue tracker](https://jira.percona.com/projects/PG/issues). You will need to create an account if you do not have one.

2. In the *Summary*, *Description*, *Steps To Reproduce*, *Affects Version* fields describe the problem you have detected. 

3. As a general rule of thumb, try to create bug reports that are:

- Reproducible: describe the steps to reproduce the problem.

- Unique: check if there already exists a JIRA ticket to describe the problem.

- Scoped to a Single Bug: only report one bug in one JIRA ticket.


## Copyright Notice

Portions Copyright © 2018-2021, Percona LLC and/or its affiliates

Portions Copyright (c) 1996-2021, PostgreSQL Global Development Group

Portions Copyright (c) 1994, The Regents of the University of California
