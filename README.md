![PostgreSQL-11](https://github.com/percona/pg_stat_monitor/workflows/postgresql-11-build/badge.svg) ![PostgreSQL-11-Package](https://github.com/percona/pg_stat_monitor/workflows/postgresql-11-package/badge.svg) ![PostgreSQL-12](https://github.com/percona/pg_stat_monitor/workflows/postgresql-12-build/badge.svg) ![PostgreSQL-12-Package](https://github.com/percona/pg_stat_monitor/workflows/postgresql-12-package/badge.svg)

![PostgreSQL-13](https://github.com/percona/pg_stat_monitor/workflows/postgresql-13-build/badge.svg) ![PostgreSQL-13-Package](https://github.com/percona/pg_stat_monitor/workflows/postgresql-13-package/badge.svg) ![PostgreSQL-14](https://github.com/percona/pg_stat_monitor/workflows/postgresql-14-build/badge.svg) 



[![Coverage Status](https://coveralls.io/repos/github/percona/pg_stat_monitor/badge.svg)](https://coveralls.io/github/percona/pg_stat_monitor)

# pg_stat_monitor: Query Performance Monitoring Tool for PostgreSQL

## Table of Contents

* [Overview](#overview)
* [Supported versions](#supported-versions)
* [Features](#features)
* [Documentation](#documentation)
* [Supported platforms](#supported-platforms)
* [Installation guidelines](#installation-guidelines)
* [Configuration](#configuration)
* [Setup](#setup)
* [Building from source code](#building-from-source)
* [How to contribute](#how-to-contribute)
* [Report a bug](#report-a-bug)
* [Support, discussions and forums](#support-discussions-and-forums)
* [License](#license)
* [Copyright notice](#copyright-notice)

## Overview

**NOTE**: This is a beta release and is subject to further changes. We recommend using it in testing environments only.

The `pg_stat_monitor` is a **_Query Performance Monitoring_** tool for PostgreSQL. It attempts to provide a more holistic picture by providing much-needed query performance insights in a single view.

`pg_stat_monitor` provides improved insights that allow database users to understand query origins, execution, planning statistics and details, query information, and metadata. This significantly improves observability, enabling users to debug and tune query performance. `pg_stat_monitor` is developed on the basis of `pg_stat_statements` as its more advanced replacement.

While `pg_stat_statements` provides ever-increasing metrics, `pg_stat_monitor` aggregates the collected data, saving user efforts for doing it themselves. `pg_stat_monitor`  stores statistics in configurable time-based units – buckets. This allows focusing on statistics generated for shorter time periods and makes query timing information such as max/min/mean time more accurate.

To learn about other features, available in `pg_stat_monitor`, see the [Features](#pg_stat_monitor-features) section and the [User Guide](https://github.com/percona/pg_stat_monitor/blob/master/docs/USER_GUIDE.md).

`pg_stat_monitor` supports PostgreSQL versions 11 and above. It is compatible with both PostgreSQL provided by PostgreSQL Global Development Group (PGDG) and [Percona Distribution for PostgreSQL](https://www.percona.com/software/postgresql-distribution).

The RPM (for RHEL and CentOS) and the DEB (for Debian and Ubuntu) packages are available from Percona repositories for PostgreSQL versions [11](https://www.percona.com/downloads/percona-postgresql-11/LATEST/), [12](https://www.percona.com/downloads/postgresql-distribution-12/LATEST/), and [13](https://www.percona.com/downloads/postgresql-distribution-13/LATEST/).

The RPM packages are also available in the official PostgreSQL (PGDG) yum repositories.

### Supported versions

The `pg_stat_monitor` should work on the latest version of both [Percona Distribution for PostgreSQL](https://www.percona.com/software/postgresql-distribution) and PostgreSQL, but is only tested with these versions:

| **Distribution** | **Version**     | **Provider** |
| ---------------- | --------------- | ------------ |
|[Percona Distribution for PostgreSQL](https://www.percona.com/software/postgresql-distribution)| [11](https://www.percona.com/downloads/percona-postgresql-11/LATEST/), [12](https://www.percona.com/downloads/postgresql-distribution-12/LATEST/) and [13](https://www.percona.com/downloads/postgresql-distribution-13/LATEST/)| Percona|
| PostgreSQL       | 11, 12, and 13 | PostgreSQL Global Development Group (PGDG) |


### Features

`pg_stat_monitor` simplifies query observability by providing a more holistic view of query from performance, application and analysis perspectives. This is achieved by grouping data in configurable time buckets that allow capturing of load and performance information for smaller time windows. So performance issues and patterns can be identified based on time and workload.


* **Time Interval Grouping:** Instead of supplying one set of ever-increasing counts, `pg_stat_monitor` computes stats for a configured number of time intervals - time buckets. This allows for much better data accuracy, especially in the case of high resolution or unreliable networks.
* **Multi-Dimensional Grouping:** While `pg_stat_statements` groups counters by userid, dbid, queryid,  `pg_stat_monitor` uses a more detailed group for higher precision. This allows a user to drill down into the performance of queries.
* **Capture Actual Parameters in the Queries:** `pg_stat_monitor` allows you to choose if you want to see queries with placeholders for parameters or actual parameter data. This simplifies debugging and analysis processes by enabling users to execute the same query.
* **Query Plan:** Each SQL is now accompanied by its actual plan that was constructed for its execution. That’s a huge advantage if you want to understand why a particular query is slower than expected.
* **Tables Access Statistics for a Statement:** This allows us to easily identify all queries that accessed a given table. This set is at par with the information provided by the `pg_stat_statements`.
* **Histogram:** Visual representation is very helpful as it can help identify issues. With the help of the histogram function, one can now view a timing/calling data histogram in response to an SQL query. And yes, it even works in psql.


### Documentation

1. [User guide](https://github.com/percona/pg_stat_monitor/blob/master/docs/USER_GUIDE.md)
2. pg_stat_monitor vs pg_stat_statements
3. pg_stat_monitor view reference
4. [Release notes](https://github.com/percona/pg_stat_monitor/blob/master/docs/RELEASE_NOTES.md)
5. Contributing guide (https://github.com/percona/pg_stat_monitor/blob/master/CONTRIBUTING.md)


### Supported platforms

The PostgreSQL YUM repository supports `pg_stat_monitor` for all [supported versions](#supported-versions) for the following platforms:

* Red Hat Enterprise/Rocky/CentOS/Oracle Linux 7 and 8
* Fedora 33 and 34

Find the list of supported platforms for `pg_stat_monitor` within [Percona Distribution for PostgreSQL](https://www.percona.com/software/postgresql-distribution) on the [Percona Release Lifecycle Overview](https://www.percona.com/services/policies/percona-software-support-lifecycle#pgsql) page.


### Installation guidelines

You can install `pg_stat_monitor` from the following sources:

* [Percona repositories](#installing-from-percona-repositories),
* [PostgreSQL PGDG yum repositories](#installing-from-postgresql-yum-repositories),
* [PGXN](#installing-from-pgxn) and
* [source code](#building-from-source).


#### Installing from Percona repositories

To install `pg_stat_monitor` from Percona repositories, you need to use the `percona-release` repository management tool.

1. [Install percona-release](https://www.percona.com/doc/percona-repo-config/installing.html) following the instructions relevant to your operating system
2. Enable Percona repository:

``` sh
percona-release setup ppgXX
```

Replace XX with the desired PostgreSQL version. For example, to install `pg_stat_monitor ` for PostgreSQL 13, specify `ppg13`.

3.  Install `pg_stat_monitor` package
    * For Debian and Ubuntu:
      ``` sh
      apt-get install percona-pg-stat-monitor13
      ```
    * For RHEL and CentOS:
      ``` sh
      yum install percona-pg-stat-monitor13
      ```

#### Installing from PostgreSQL `yum` repositories

Install the PostgreSQL repositories following the instructions in the [Linux downloads (Red Hat family)](https://www.postgresql.org/download/linux/redhat/) chapter in PostgreSQL documentation.

Install `pg_stat_monitor`:

```
dnf install -y pg_stat_monitor_<VERSION>
```

Replace the `VERSION` variable with the PostgreSQL version you are using (e.g. specify `pg_stat_monitor_13` for PostgreSQL 13)


#### Installing from PGXN

You can install `pg_stat_monitor` from PGXN (PostgreSQL Extensions Network) using the [PGXN client](https://pgxn.github.io/pgxnclient/).

Use the following command:

```
pgxn install pg_stat_monitor
```

### Configuration

You can find the configuration parameters of the `pg_stat_monitor` extension in the `pg_stat_monitor_settings` view. To change the default configuration, specify new values for the desired parameters using the GUC (Grant Unified Configuration) system. To learn more, refer to the [Configuration](https://github.com/percona/pg_stat_monitor/blob/master/docs/USER_GUIDE.md#configuration) section of the user guide.


### Setup

You can enable `pg_stat_monitor` when your `postgresql` instance is not running.

`pg_stat_monitor` needs to be loaded at the start time. The extension requires additional shared memory; therefore,  add the `pg_stat_monitor` value for the `shared_preload_libraries` parameter and restart the `postgresql` instance.

Use the [ALTER SYSTEM](https://www.postgresql.org/docs/current/sql-altersystem.html)command from `psql` terminal to modify the `shared_preload_libraries` parameter.

```sql
ALTER SYSTEM SET shared_preload_libraries = 'pg_stat_monitor';
```

> **NOTE**: If you’ve added other modules to the `shared_preload_libraries` parameter (for example, `pg_stat_statements`), list all of them separated by commas for the `ALTER SYSTEM` command. 
>
>:warning: For PostgreSQL 13 and earlier versions,`pg_stat_monitor` **must** follow `pg_stat_statements`. For example, `ALTER SYSTEM SET shared_preload_libraries = 'foo, pg_stat_statements, pg_stat_monitor'`.
>
>In PostgreSQL 14, modules can be specified in any order.

Start or restart the `postgresql` instance to apply the changes.

* On Debian and Ubuntu:

```sh
sudo systemctl restart postgresql.service
```

* On Red Hat Enterprise Linux and CentOS:


```sh
sudo systemctl restart postgresql-13
```

Create the extension using the [CREATE EXTENSION](https://www.postgresql.org/docs/current/sql-createextension.html) command. Using this command requires the privileges of a superuser or a database owner. Connect to `psql` as a superuser for a database and run the following command:


```sql
CREATE EXTENSION pg_stat_monitor;
```


This allows you to see the stats collected by `pg_stat_monitor`.

By default, `pg_stat_monitor` is created for the `postgres` database. To access the statistics from other databases, you need to create the extension for every database.

```
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

To learn more about `pg_stat_monitor` features and usage, see [User Guide](https://github.com/percona/pg_stat_monitor/blob/master/docs/USER_GUIDE.md). To view all other data elements provided by `pg_stat_monitor`, please see the reference.


### Building from source

To build `pg_stat_monitor` from source code, you require the following:

* git
* make
* gcc
* pg_config

You can download the source code of the latest release of `pg_stat_monitor` from [the releases page on GitHub](https://github.com/Percona/pg_stat_monitor/releases) or using git:


```
git clone git://github.com/Percona/pg_stat_monitor.git
```

Compile and install the extension

```
cd pg_stat_monitor
make USE_PGXS=1
make USE_PGXS=1 install
```

### How to contribute

We welcome and strongly encourage community participation and contributions, and are always looking for new members that are as dedicated to serving the community as we are.

The [Contributing Guide](https://github.com/percona/pg_stat_monitor/blob/master/CONTRIBUTING.md) contains the guidelines on how you can contribute.

### Report a bug

If you would like to suggest a new feature / an improvement or you found a bug in `pg_stat_monitor`, please submit the report to the [Percona Jira issue tracker](https://jira.percona.com/projects/PG). 

Refer to the [Submit a bug report or a feature request](https://github.com/percona/pg_stat_monitor/blob/master/CONTRIBUTING.md#submit-a-bug-report-or-a-feature-request) section for bug reporting guidelines.


### Support, discussions and forums

We welcome your feedback on your experience with `pg_stat_monitor`. Join our [technical forum](https://forums.percona.com/) or [Discord](https://discord.gg/mQEyGPkNbR) channel for help with `pg_stat_monitor` and Percona's open source software for MySQL®, [PostgreSQL](https://www.percona.com/software/postgresql-distribution), and MongoDB® databases.


### License

This project is licensed under the same open liberal terms and conditions as the PostgreSQL project itself. Please refer to the [LICENSE](https://github.com/percona/pg_stat_monitor/blob/master/LICENSE) file for more details.


### Copyright notice

* Portions Copyright © 2018-2021, Percona LLC and/or its affiliates
* Portions Copyright (c) 1996-2021, PostgreSQL Global Development Group
* Portions Copyright (c) 1994, The Regents of the University of California
