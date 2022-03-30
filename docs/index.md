# Welcome

The **pg_stat_monitor** is a **_Query Performance Monitoring_** tool for PostgreSQL. It collects performance stats and provides query performance insights in a single view. These insights allow database users to understand query origins, execution, planning statistics and details, query information, and metadata. This significantly improves observability, enabling users to debug and tune query performance. 


## How `pg_stat_monitor` works?

`pg_stat_monitor` is developed on the basis of `pg_stat_statements` as its more advanced replacement. While `pg_stat_statements` provides ever-increasing metrics, `pg_stat_monitor` aggregates the collected data, saving user efforts for doing it themselves. `pg_stat_monitor`  stores statistics in configurable time-based units – _buckets_. Such bucket-based data collection allows focusing on statistics generated for shorter time periods and makes query timing information such as max/min/mean time more accurate.

## Features

* **Time Interval Grouping:** Instead of supplying one set of ever-increasing counts, `pg_stat_monitor` computes stats for a configured number of time intervals - time buckets. This allows for much better data accuracy, especially in the case of high resolution or unreliable networks.
* **Multi-Dimensional Grouping:** While `pg_stat_statements` groups counters by userid, dbid, queryid,  `pg_stat_monitor` uses a more detailed group for higher precision. This allows a user to drill down into the performance of queries.
* **Capture Actual Parameters in the Queries:** `pg_stat_monitor` allows you to choose if you want to see queries with placeholders for parameters or actual parameter data. This simplifies debugging and analysis processes by enabling users to execute the same query.
* **Query Plan:** Each SQL is now accompanied by its actual plan that was constructed for its execution. That's a huge advantage if you want to understand why a particular query is slower than expected.
* **Tables Access Statistics for a Statement:** This allows us to easily identify all queries that accessed a given table. This set is at par with the information provided by the `pg_stat_statements`.
* **Histogram:** Visual representation is very helpful as it can help identify issues. With the help of the histogram function, one can now view a timing/calling data histogram in response to an SQL query. And yes, it even works in psql.


## Availability 

`pg_stat_monitor` supports PostgreSQL versions 11 and above. It is compatible with both PostgreSQL provided by PostgreSQL Global Development Group (PGDG) and [Percona Distribution for PostgreSQL](https://www.percona.com/software/postgresql-distribution).


### Supported  versions

The `pg_stat_monitor` should work on the latest version of both [Percona Distribution for PostgreSQL](https://www.percona.com/software/postgresql-distribution) and PostgreSQL, but is only tested with these versions:

| **Distribution** | **Version**     | **Provider** |
| ---------------- | --------------- | ------------ |
|[Percona Distribution for PostgreSQL](https://www.percona.com/software/postgresql-distribution)| [11](https://www.percona.com/downloads/percona-postgresql-11/LATEST/), [12](https://www.percona.com/downloads/postgresql-distribution-12/LATEST/), [13](https://www.percona.com/downloads/postgresql-distribution-13/LATEST/) and [14](https://www.percona.com/downloads/postgresql-distribution-14/LATEST/)| Percona|
| PostgreSQL       | 11, 12, 13 and 14 | PostgreSQL Global Development Group (PGDG) |

The RPM (for RHEL and CentOS) and the DEB (for Debian and Ubuntu) packages are available from Percona repositories for PostgreSQL versions [11](https://www.percona.com/downloads/percona-postgresql-11/LATEST/), [12](https://www.percona.com/downloads/postgresql-distribution-12/LATEST/), [13](https://www.percona.com/downloads/postgresql-distribution-13/LATEST/) and [14](https://www.percona.com/downloads/postgresql-distribution-14/LATEST/).

The RPM packages are also available in the official PostgreSQL (PGDG) `yum` repositories.

## Get started

* Use the [installation guidelines](setup.md) to install and set up `pg_stat_monitor`.
* Refer to the [User guide](USER_GUIDE.md) for details about available features and functions, usage examples  and configuration parameters.

## Read more

* [`pg_stat_monitor` view reference](REREFENCE.md)
* [pg_stat_monitor and pg_stat_statements comparison](COMPARE.md)
 

