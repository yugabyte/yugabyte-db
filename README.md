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

There are two ways to install ``pg_stat_monitor``:
- by downloading the pg_stat_monitor source code and compiling it. 
- by downloading the ``deb`` or ``rpm`` packages.

**Compile from the source code**

The latest release of ``pg_stat_monitor`` can be downloaded from [this GitHub page](https://github.com/Percona/pg_stat_monitor/releases) or it can be downloaded using the git:

```
git clone git://github.com/Percona/pg_stat_monitor.git
```

After downloading the code, set the path for the PostgreSQL binary.

The release notes can be find [here](https://github.com/percona/pg_stat_monitor/blob/master/RELEASE_NOTES.md).

Compile and Install the extension

```
cd pg_stat_monitor

make USE_PGXS=1

make USE_PGXS=1 install
```

**Installing from rpm/deb packages**

``pg_stat_monitor`` is supplied as part of Percona Distribution for PostgreSQL. The rpm/deb packages are available from Percona repositories. Refer to [Percona Documentation](https://www.percona.com/doc/postgresql/LATEST/installing.html) for installation instructions.


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

```
postgres=# alter system set shared_preload_libraries=pg_stat_monitor;
ALTER SYSTEM

sudo systemctl restart postgresql-11
```


Create the extension using the ``create extension`` command.

```sql
postgres=# create extension pg_stat_monitor;
CREATE EXTENSION
```

## Copyright Notice

Copyright (c) 2006 - 2020, Percona LLC.
