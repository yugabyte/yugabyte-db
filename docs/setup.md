# Setting up `pg_stat_monitor`

## Supported platforms

The PostgreSQL YUM repository supports `pg_stat_monitor` for all [supported versions](#supported-versions) for the following platforms:

* Red Hat Enterprise/Rocky/CentOS/Oracle Linux 7 and 8
* Fedora 33 and 34

Find the list of supported platforms for `pg_stat_monitor` within [Percona Distribution for PostgreSQL](https://www.percona.com/software/postgresql-distribution) on the [Percona Release Lifecycle Overview](https://www.percona.com/services/policies/percona-software-support-lifecycle#pgsql) page.


## Installation guidelines

You can install `pg_stat_monitor` from the following sources:

* [Percona repositories](#installing-from-percona-repositories),
* [PostgreSQL PGDG yum repositories](#installing-from-postgresql-yum-repositories),
* [PGXN](#installing-from-pgxn) and
* [source code](#building-from-source).


### Installing from Percona repositories

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

### Installing from PostgreSQL `yum` repositories

Install the PostgreSQL repositories following the instructions in the [Linux downloads (Red Hat family)](https://www.postgresql.org/download/linux/redhat/) chapter in PostgreSQL documentation.

Install `pg_stat_monitor`:

```
dnf install -y pg_stat_monitor_<VERSION>
```

Replace the `VERSION` variable with the PostgreSQL version you are using (e.g. specify `pg_stat_monitor_13` for PostgreSQL 13)


### Installing from PGXN

You can install `pg_stat_monitor` from PGXN (PostgreSQL Extensions Network) using the [PGXN client](https://pgxn.github.io/pgxnclient/).

Use the following command:

```
pgxn install pg_stat_monitor
```

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


## Setup

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
>In PostgreSQL 14, you can specify `pg_stat_statements` and `pg_stat_monitor` in any order. However, due to the extensions' architecture, if both `pg_stat_statements` and `pg_stat_monitor` are loaded, only the last listed extension captures utility queries, CREATE TABLE, Analyze, etc. The first listed extension captures  most common queries like SELECT, UPDATE, INSERT, but does not capture utility queries.
>
>Thus, to collect the whole statistics with pg_stat_monitor, we recommend to specify the extensions as follows: ALTER SYSTEM SET shared_preload_libraries = 'pg_stat_statements, pg_stat_monitor'.

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

To learn more about `pg_stat_monitor` features and usage, see [User Guide](https://github.com/percona/pg_stat_monitor/blob/master/docs/USER_GUIDE.md). To view all other data elements provided by `pg_stat_monitor`, please see the [`pg_stat_monitor` view reference](REREFENCE.md).


## Configuration

You can find the configuration parameters of the `pg_stat_monitor` extension in the `pg_stat_monitor_settings` view. To change the default configuration, specify new values for the desired parameters using the GUC (Grant Unified Configuration) system. To learn more, refer to the [Configuration](https://github.com/percona/pg_stat_monitor/blob/master/docs/USER_GUIDE.md#configuration) section of the user guide.

## Remove `pg_stat_monitor`

To uninstall `pg_stat_monitor`, do the following:

1. Disable statistics collection. Establish the `psql` session and run the following command :

    ```sql
    ALTER SYSTEM SET pg_stat_monitor.pgsm_enable = 0;
    ```

2. Drop `pg_stat_monitor` extension:

    ```sql
    DROP EXTENSION pg_stat_monitor;
    ```

3. Remove `pg_stat_monitor` from the `shared_preload_libraries` configuration parameter:

    ```sql 
    ALTER SYSTEM SET shared_preload_libraries = '';
    ```

    !!! important

        If the `shared_preload_libraries` parameter includes other modules, specify them all for the `ALTER SYSTEM SET` command to keep using them.

4. Restart the `postgresql` instance to apply the changes. The following command restarts PostgreSQL 13. Replace the version value with the one you are using. 

    * On Debian and Ubuntu:

    ```sh
    sudo systemctl restart postgresql.service
    ```

    * On Red Hat Enterprise Linux and CentOS:


    ```sh
    sudo systemctl restart postgresql-13
    ```


