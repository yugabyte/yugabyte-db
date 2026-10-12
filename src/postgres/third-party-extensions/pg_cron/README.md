[![Citus Banner](/github-banner.png)](https://www.citusdata.com/)

# What is pg_cron?

pg_cron is a simple cron-based job scheduler for PostgreSQL (10 or higher) that runs inside the database as an extension. 

---

# Contents
- [How pg_cron works](#how-pg_cron-works)
- [Cron syntax](#cron-syntax)
- [Managing and creating jobs](#managing-and-creating-jobs)
	- [Creating a cron job](#creating-a-cron-job)
	- [Creating a cron job in a different database](#creating-a-cron-job-in-a-different-database)
	- [Removing a cron job](#removing-a-cron-job)
	- [Altering a cron job](#altering-a-cron-job)
- [Installing pg_cron](#installing-pg_cron)
- [Setting up pg_cron](#setting-up-pg_cron)
- [Monitoring jobs](#monitoring-jobs)
- [Example use cases](#example-use-cases)
- [Managed services](#managed-services)
- [Code of Conduct](#code-of-conduct)

---

# How pg_cron works

The extension creates a background worker that tracks jobs in the `cron.job` table.

```sql
CREATE TABLE cron.job (
	jobid bigint primary key default pg_catalog.nextval('cron.jobid_seq'),
	schedule text not null,
	command text not null,
	nodename text not null default 'localhost',
	nodeport int not null default pg_catalog.inet_server_port(),
	database text not null default pg_catalog.current_database(),
	username text not null default current_user
);
```

Based on your configurations, to execute a job, the extension establishes a Postgres connection or spawns a database worker. 

pg_cron can run multiple jobs in parallel, but only one instance of each specific job at a time. If a second instance is triggered before the first finishes, it’s queued and starts as soon as the first one completes.

# Cron syntax

The code in pg_cron that handles parsing and scheduling comes directly from the [cron source code by Paul Vixie](https://github.com/vixie/cron), hence the same options are supported.
```
 ┌───────────── min (0 - 59)
 │ ┌────────────── hour (0 - 23)
 │ │ ┌─────────────── day of month (1 - 31) or last day of the month ($)
 │ │ │ ┌──────────────── month (1 - 12)
 │ │ │ │ ┌───────────────── day of week (0 - 6) (0 to 6 are Sunday to
 │ │ │ │ │                  Saturday, or use names; 7 is also Sunday)
 │ │ │ │ │
 │ │ │ │ │
 * * * * *
```

An easy way to create a cron schedule is: [crontab.guru](http://crontab.guru/).

pg_cron also allows you:
- to use `$` to indicate last day of the month.
- to use `[1-59] seconds` to schedule a job based on an interval. Note, you cannot use seconds with the other time units.


Example cron schedules:

```
'10 seconds'  # every 10 seconds
* * * * *     # every minute
*/5 * * * *   # every 5 minutes
0 * * * *     # every hour
0 0 * * *     # daily at 12AM
0 0 * * 1-5   # 12AM every weekday
0 1 * * 0     # 1AM every Sunday
0 13 2 6 *    # 1PM on the 2nd of June
```

# Managing and creating jobs

Cron jobs can be managed by directly interacting with the `cron.job` table if you have the required permissions. However, it is recommended to use the cron functions:
- [`cron.schedule`](#creating-a-cron-job)
- [`cron.schedule_in_database`](#creating-a-cron-job-in-a-different-database)
- [`cron.unschedule`](#removing-a-cron-job)
- [`cron.alter_job`](#altering-a-cron-job)

> Note, an [RLS policy](https://www.postgresql.org/docs/current/ddl-rowsecurity.html) ensures that jobs can only be seen and modified by the user that created them, unless the user is a superuser or has the `bypassrls` attribute.

### Creating a cron job

#### `cron.schedule` signatures
```sql
-- create job, return jobid
CREATE OR REPLACE FUNCTION cron.schedule(schedule text, command text)
RETURNS bigint;

-- create named job, return jobid
CREATE OR REPLACE FUNCTION cron.schedule(job_name text, schedule text, command text)
RETURNS bigint
```

#### Examples

##### Create a cron job

```sql
-- Delete old data on Saturday at 3:30am (GMT)
SELECT cron.schedule(
       '30 3 * * 6', 
       $$DELETE FROM events WHERE event_time < now() - interval '1 week'$$
);
-- returns cron id
```

##### Create a named cron job

```sql
-- Vacuum every day at 10:00am (GMT)
SELECT cron.schedule(
       'nightly-vacuum', 
       '0 10 * * *', 
       'VACUUM'
);
-- returns cron id
```

##### Create a job that runs every 30 seconds

```sql
-- run SELECT 1 every 30 seconds
SELECT cron.schedule(
       'run_every_30_seconds', 
       '30 seconds', 
       'SELECT 1'
);
-- returns cron id
```

##### Create a job that calls a stored procedure every 5 seconds

```sql
-- Call a stored procedure every 5 seconds
SELECT cron.schedule(
       'process-updates',
       '5 seconds',
       'CALL process_updates()'
);
-- returns cron id
```

##### Create a job that processes payroll at 12:00 of the last day of each month

```sql
-- Process payroll at 12:00 of the last day of each month
SELECT cron.schedule(
       'process-payroll',
       '0 12 $ * *',
       'CALL process_payroll()'
);
-- returns cron id
```

### Creating a cron job in a different database

#### `cron.schedule_in_database` signature
```sql
-- create job, return jobid
CREATE OR REPLACE FUNCTION cron.schedule_in_database(
       job_name text, 
       schedule text, 
       command text, 
       database text, 
       username text DEFAULT NULL::text, 
       active boolean DEFAULT true
)
RETURNS bigint
```

#### Example

##### Create a cron job in a different database

```sql
-- Delete old data on Saturday at 3:30am (GMT)
SELECT cron.schedule_in_database(
       'delete_old_data', 
       '30 3 * * 6', 
       $$DELETE FROM events WHERE event_time < now() - interval '1 week'$$,
       'some_other_database'
);
-- returns cron id
```

### Removing a cron job

#### `cron.unschedule` signatures
```sql
-- remove job by name, return true if job was removed
CREATE OR REPLACE FUNCTION cron.unschedule(job_name text)
RETURNS boolean

-- remove job by id, return true if job was removed
CREATE OR REPLACE FUNCTION cron.unschedule(job_id bigint)
RETURNS boolean
```

#### Examples

##### Remove a named cron job

```sql
-- delete job by name
SELECT cron.unschedule('nightly-vacuum');
-- returns true if job was removed
```

##### Remove a cron job by id

```sql
-- delete job by id
SELECT cron.unschedule(42);
-- returns true if job was removed
```

### Altering a cron job

#### `cron.alter_job` signature
```sql
CREATE OR REPLACE FUNCTION cron.alter_job(
       job_id bigint, 
       schedule text DEFAULT NULL::text, 
       command text DEFAULT NULL::text, 
       database text DEFAULT NULL::text, 
       username text DEFAULT NULL::text, 
       active boolean DEFAULT NULL::boolean
)
RETURNS void
```

#### Examples

##### Change a job's schedule

```sql
-- change job's schedule
SELECT cron.alter_job(42, '0 10 * * *');
-- returns void
```

##### Change a job's, schedule, command, and username

```sql
-- change job's command
SELECT cron.alter_job(
       42,
       '0 10 * * *',
       'VACUUM',
       username := 'some_other_user'
);
-- returns void
```

##### Deactivate a job

```sql
-- deactivate job
SELECT cron.alter_job(42, active := false);
-- returns void
```

# Installing pg_cron

Install on Red Hat, CentOS, Fedora, Amazon Linux with PostgreSQL 18 using [PGDG](https://yum.postgresql.org/repopackages/):

```bash
# Install the pg_cron extension
sudo yum install -y pg_cron_18
```

Install on Debian, Ubuntu with PostgreSQL 18 using [apt.postgresql.org](https://wiki.postgresql.org/wiki/Apt):

```bash
# Install the pg_cron extension
sudo apt-get -y install postgresql-18-cron
```

You can also install pg_cron by building it from source:

```bash
git clone https://github.com/citusdata/pg_cron.git
cd pg_cron
# Ensure pg_config is in your path, e.g.
export PATH=/usr/pgsql-18/bin:$PATH
make && sudo PATH=$PATH make install
```

## Windows

Ensure [C++ support in Visual Studio](https://learn.microsoft.com/en-us/cpp/build/building-on-the-command-line?view=msvc-170#download-and-install-the-tools) is installed and run `x64 Native Tools Command Prompt for VS [version]` as administrator. Then use `nmake` to build:

```cmd
set "PGROOT=C:\Program Files\PostgreSQL\18"
cd %TMP%
git clone https://github.com/citusdata/pg_cron.git
cd pg_cron
nmake /F Makefile.win
nmake /F Makefile.win install
```

### Installation Notes - Windows

#### Missing Header

If compilation fails with `Cannot open include file: 'postgres.h': No such file or directory`, make sure `PGROOT` is correct.

#### Permissions

If installation fails with `Access is denied`, re-run the installation instructions as an administrator.

# Setting up pg_cron

To start the pg_cron background worker, you need to add pg_cron to `shared_preload_libraries` in postgresql.conf. Note that pg_cron does not run any jobs as a long a server is in [hot standby](https://www.postgresql.org/docs/current/static/hot-standby.html) mode, but it automatically starts when the server is promoted.

```
# add to postgresql.conf

# required to load pg_cron background worker on start-up
shared_preload_libraries = 'pg_cron'
```

By default, the pg_cron background worker expects its metadata tables to be created in the "postgres" database. However, you can configure this by setting the `cron.database_name` configuration parameter in postgresql.conf.
```
# add to postgresql.conf

# optionally, specify the database in which the pg_cron background worker should run (defaults to postgres)
cron.database_name = 'postgres'
```
`pg_cron` may only be installed to one database in a cluster. If you need to run jobs in multiple databases, use `cron.schedule_in_database()`.

Previously pg_cron could only use GMT time, but now you can adapt your time by setting `cron.timezone` in postgresql.conf.
```
# add to postgresql.conf

# optionally, specify the timezone in which the pg_cron background worker should run (defaults to GMT). E.g:
cron.timezone = 'PRC'
```

After restarting PostgreSQL, you can create the pg_cron functions and metadata tables using `CREATE EXTENSION pg_cron`.

```sql
-- run as superuser:
CREATE EXTENSION pg_cron;

-- optionally, grant usage to regular users:
GRANT USAGE ON SCHEMA cron TO marco;
```

### Ensuring pg_cron can start jobs

**Important**: By default, pg_cron uses libpq to open a new connection to the local database, which needs to be allowed by [pg_hba.conf](https://www.postgresql.org/docs/current/static/auth-pg-hba-conf.html). 
It may be necessary to enable `trust` authentication for connections coming from localhost in  for the user running the cron job, or you can add the password to a [.pgpass file](https://www.postgresql.org/docs/current/static/libpq-pgpass.html), which libpq will use when opening a connection. 

You can also use a unix domain socket directory as the hostname and enable `trust` authentication for local connections in [pg_hba.conf](https://www.postgresql.org/docs/current/static/auth-pg-hba-conf.html), which is normally safe:
```
# Connect via a unix domain socket:
cron.host = '/tmp'

# Can also be an empty string to look for the default directory:
cron.host = ''
```

Alternatively, pg_cron can be configured to use background workers. In that case, the number of concurrent jobs is limited by the `max_worker_processes` setting, so you may need to raise that.

```
# Schedule jobs via background workers instead of localhost connections
cron.use_background_workers = on
# Increase the number of available background workers from the default of 8
max_worker_processes = 20
```

For security, jobs are executed in the database in which the `cron.schedule` function is called with the same permissions as the current user. In addition, users are only able to see their own jobs in the `cron.job` table.

```sql
-- View active jobs
select * from cron.job;
```

## Extension settings

The pg_cron extension supports the following configuration parameters:

| Setting                          | Default     | Description                                                                              |
| ---------------------------------| ----------- | ---------------------------------------------------------------------------------------- |
| `cron.database_name`             | `postgres`  | Database in which the pg_cron background worker should run.                              |
| `cron.enable_superuser_jobs`     | `on`        | Allow jobs to be scheduled as superusers.                                                |
| `cron.host`                      | `localhost` | Hostname to connect to postgres.                                                         |
| `cron.launch_active_jobs`        | `on`        | When off, disables all active jobs without requiring a server restart                    |
| `cron.log_min_messages`          | `WARNING`   | log_min_messages for the launcher bgworker.                                              |
| `cron.log_run`                   | `on`        | Log all run details in the`cron.job_run_details` table.                                  |
| `cron.log_statement`             | `on`        | Log all cron statements prior to execution.                                              |
| `cron.max_running_jobs`          | `32`        | Maximum number of jobs that can be running at the same time.                             |
| `cron.timezone`                  | `GMT`       | Timezone in which the pg_cron background worker should run.                              |
| `cron.use_background_workers`    | `off`       | Use background workers instead of client connections.                                    |

### Changing settings

To view setting configurations, run:

```sql
SELECT * FROM pg_settings WHERE name LIKE 'cron.%';
```

Setting can be changed in the postgresql.conf file or with the below command:

```sql
ALTER SYSTEM SET cron.<parameter> TO '<value>';
```

`cron.log_min_messages` and `cron.launch_active_jobs` have a [setting context](https://www.postgresql.org/docs/current/view-pg-settings.html#VIEW-PG-SETTINGS) of `sighup`. They can be finalized by executing `SELECT pg_reload_conf();`.

All the other settings have a postmaster context and only take effect after a server restart.

# Monitoring jobs

### Reviewing the `cron.job_run_details` table

You can view job activity in the `cron.job_run_details` table:

```sql
select * from cron.job_run_details order by start_time desc limit 5;
┌───────┬───────┬─────────┬──────────┬──────────┬───────────────────┬───────────┬──────────────────┬───────────────────────────────┬───────────────────────────────┐
│ jobid │ runid │ job_pid │ database │ username │      command      │  status   │  return_message  │          start_time           │           end_time            │
├───────┼───────┼─────────┼──────────┼──────────┼───────────────────┼───────────┼──────────────────┼───────────────────────────────┼───────────────────────────────┤
│    11 │  4328 │    2610 │ postgres │ marco    │ select pg_sleep(3)│ running   │ NULL             │ 2023-02-07 09:30:00.098164+01 │ NULL                          │
│    10 │  4327 │    2609 │ postgres │ marco    │ select process()  │ succeeded │ SELECT 1         │ 2023-02-07 09:29:00.015168+01 │ 2023-02-07 09:29:00.832308+01 │
│    10 │  4321 │    2603 │ postgres │ marco    │ select process()  │ succeeded │ SELECT 1         │ 2023-02-07 09:28:00.011965+01 │ 2023-02-07 09:28:01.420901+01 │
│    10 │  4320 │    2602 │ postgres │ marco    │ select process()  │ failed    │ server restarted │ 2023-02-07 09:27:00.011833+01 │ 2023-02-07 09:27:00.72121+01  │
│     9 │  4320 │    2602 │ postgres │ marco    │ select do_stuff() │ failed    │ job canceled     │ 2023-02-07 09:26:00.011833+01 │ 2023-02-07 09:26:00.22121+01  │
└───────┴───────┴─────────┴──────────┴──────────┴───────────────────┴───────────┴──────────────────┴───────────────────────────────┴───────────────────────────────┘
(10 rows)
```

The records in the table are not cleaned automatically, but every user that can schedule cron jobs also has permission to delete their own `cron.job_run_details` records. 

Especially when you have jobs that run every few seconds, it can be a good idea to clean up regularly, which can easily be done using pg_cron itself:

```sql
-- Delete old cron.job_run_details records of the current user every day at noon
SELECT  cron.schedule('delete-job-run-details', '0 12 * * *', $$DELETE FROM cron.job_run_details WHERE end_time < now() - interval '7 days'$$);
```

If you do not want to use `cron.job_run_details` at all, then you can add `cron.log_run = off` to `postgresql.conf`.

### Other cron logging settings

If the `cron.log_statement` setting is configured, jobs will be logged before execution. The `cron.log_min_messages` setting controls the [minimum level of messages](https://www.postgresql.org/docs/current/runtime-config-logging.html#RUNTIME-CONFIG-SEVERITY-LEVELS) that will be recorded.

# Example use cases

Articles showing possible ways of using pg_cron:

* [Auto-partitioning using pg_partman](https://www.citusdata.com/blog/2018/01/24/citus-and-pg-partman-creating-a-scalable-time-series-database-on-postgresql/)
* [Computing rollups in an analytical dashboard](https://www.citusdata.com/blog/2017/12/27/real-time-analytics-dashboards-with-citus/)
* [Deleting old data, vacuum](https://www.citusdata.com/blog/2016/09/09/pgcron-run-periodic-jobs-in-postgres/)
* [Feeding cats](http://bonesmoses.org/2016/09/09/pg-phriday-irrelevant-inclinations/)
* [Routinely invoking a function](https://fluca1978.github.io/2019/05/21/pgcron.html)
* [Postgres as a cron server](https://supabase.io/blog/2021/03/05/postgres-as-a-cron-server)

# Managed services

The following table keeps track of which of the major managed Postgres services support pg_cron.

| Service       | Supported     |
| ------------- |:-------------:|
| [Aiven](https://aiven.io/postgresql) | :heavy_check_mark: |
| [Alibaba Cloud](https://www.alibabacloud.com/help/doc-detail/150355.htm) | :heavy_check_mark: |
| [Amazon RDS](https://aws.amazon.com/rds/postgresql/)     | :heavy_check_mark:      |          |
| [Azure](https://azure.microsoft.com/en-us/services/postgresql/) | :heavy_check_mark:  |
| [Clickhouse](https://clickhouse.com/docs/cloud/managed-postgres/extensions)  | :heavy_check_mark:  |
| [Crunchy Bridge](https://www.crunchydata.com/products/crunchy-bridge/?ref=producthunt) | :heavy_check_mark: |
| [DigitalOcean](https://www.digitalocean.com/products/managed-databases/) | :heavy_check_mark: |
| [Google Cloud](https://cloud.google.com/sql/postgresql/) | :heavy_check_mark: |
| [Heroku](https://elements.heroku.com/addons/heroku-postgresql) | :x: |
| [Instaclustr](https://instaclustr.com) | :heavy_check_mark:  |
| [Lakebase](https://docs.databricks.com/aws/en/oltp/projects/extensions) | :x: |
| [Neon](https://neon.tech/docs/extensions/extensions-intro#tooling-admin) | :heavy_check_mark: |
| [PlanetScale](https://planetscale.com/docs/postgres/extensions) | :heavy_check_mark: |
| [ScaleGrid](https://scalegrid.io/postgresql.html) | :heavy_check_mark:  |
| [Scaleway](https://www.scaleway.com/en/database/) | :heavy_check_mark:  |
| [Snowflake](https://www.scaleway.com/en/database/) | :heavy_check_mark:  |
| [Supabase](https://supabase.io/docs/guides/database) | :heavy_check_mark:  |
| [Ubicloud](https://www.ubicloud.com/docs/managed-postgresql/extensions) | :heavy_check_mark:  |
| [YugabyteDB](https://www.yugabyte.com/) | :heavy_check_mark:  |

# Code of Conduct

This project has adopted the [Microsoft Open Source Code of Conduct](https://opensource.microsoft.com/codeofconduct/). For more information see the [Code of Conduct FAQ](https://opensource.microsoft.com/codeofconduct/faq/) or contact [opencode@microsoft.com](mailto:opencode@microsoft.com) with any additional questions or comments.
