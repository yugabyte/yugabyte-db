---
title: pg_cron extension
headerTitle: pg_cron extension
linkTitle: pg_cron
description: Using the pg_cron extension in YugabyteDB
techPreview: /preview/releases/versioning/#feature-availability
menu:
  preview:
    identifier: extension-pgcron
    parent: pg-extensions
    weight: 20
type: docs
---

The [pg_cron](https://github.com/citusdata/pg_cron) extension provides a cron-based job scheduler that runs inside the database. It uses the same syntax as regular cron, and allows you to schedule YSQL commands directly from the database. You can also use '[1-59] seconds' to schedule a job based on an interval.

YugabyteDB supports all features of the pg_cron extension. Although YugabyteDB is a distributed database that operates on multiple nodes, pg_cron only runs on one of these nodes, called the pg_cron leader. Only the pg_cron leader schedules and runs the cron jobs. The queries executed by jobs do take advantage of all available resources in the cluster.

If the pg_cron leader node fails, another node is automatically elected as the new leader to ensure it is highly available. This process is transparent, and you can connect to any node in a cluster to schedule jobs.

## Set up pg_cron

pg_cron in YugabyteDB is {{<badge/tp>}}. Before you can use the feature, you must enable it by setting the `enable_pg_cron` flag. To do this, add `enable_pg_cron` to the `allowed_preview_flags_csv` flag and set the `enable_pg_cron` flag to true on all YB-Masters and YB-TServers.

The pg_cron extension is installed on only one database, which stores the extension data. The default cron database is `yugabyte`. You can change it by setting the `ysql_cron_database_name` flag on all YB-TServers. You can create the database after setting the flag.

For example, to create a single-node [yugabyted](../../../../reference/configuration/yugabyted/) cluster with pg_cron on database 'db1', use the following command:

```sh
./bin/yugabyted start --master_flags "allowed_preview_flags_csv={enable_pg_cron},enable_pg_cron=true" --tserver_flags "allowed_preview_flags_csv={enable_pg_cron},enable_pg_cron=true,ysql_cron_database_name=db1" --ui false
```

To change the database after the extension is created, you must first drop the extension and then change the flag value.

## Enable pg_cron

Create the extension as superuser on the cron database.

```sql
CREATE EXTENSION pg_cron;
```

You can grant access to other users to use the extension. For example:

```sql
GRANT USAGE ON SCHEMA cron TO elephant;
```

## Use pg_cron

YugabyteDB supports all features and syntax of the pg_cron extension.

For example, the following command calls a stored procedure every five seconds:

```sql
SELECT cron.schedule('process-updates', '5 seconds', 'CALL process_updates()');
```

If you need to run jobs in multiple databases, use `cron.schedule_in_database()`.

When running jobs, keep in mind the following:

- It may take up to 60 seconds for job changes to get picked up by the pg_cron leader.
- When a new pg_cron leader node is elected, no jobs are run for the first minute. Any job that were in flight on the failed node will not be retried, as their outcome is not known.

For more information on how to schedule jobs, refer to the [pg_cron documentation](https://github.com/yugabyte/yugabyte-db/blob/master/src/postgres/third-party-extensions/pg_cron/README.md).

## Best practices

The `cron.job_run_details` table is part of the `pg_cron` extension in PostgreSQL. This table logs information about each cron job run, including its start and end time, status, and any exit messages or errors that occurred during the execution. The records in `cron.job_run_details` are not cleaned automatically, so in scenarios where you have jobs that run frequently, set up a periodic cleanup task for the table using `pg_cron` to ensure old data doesn't accumulate and affect database performance.

### View job details

You can view the status of running and recently completed jobs in the `cron.job_run_details` table using the following command:

```sql
select * from cron.job_run_details order by start_time desc limit 5;
```

### Set up a periodic cleanup task

Create a periodoc cleaning task for the `cron.job_run_details` table using `pg_cron` similar to the following example:

```sql
-- Delete old cron.job_run_details records of the current user every day at noon
SELECT  cron.schedule('delete-job-run-details', '0 12 * * *', $$DELETE FROM cron.job_run_details WHERE end_time < now() - interval '7 days'$$);
```

## Examples

The following examples decribe various ways `pg_cron` can be used to automate and improve database management tasks. The tool can help maintain database performance, consistency, and reliability through scheduled jobs.

### Monitor and identify slow queries

Use [pg_stat_statements](../extension-pgstatstatements/) to capture statistics about queries, and schedule regular reports with `pg_cron` to summarize slow queries.

```sql
INSERT INTO slow_queries SELECT * FROM pg_stat_statements ORDER BY total_time DESC LIMIT 10;
```

### Validate performance

`pg_cron` can indirectly assist with query tuning in a few ways. For example, after identifying and implementing query optimizations or indexing improvements, you can monitor the impact over time using `pg_cron` to execute scripts that validate the effectiveness of your changes.

```sql
SELECT cron.schedule('weekly_check_performance', '0 4 * * 0',
$$
DO $$
BEGIN
-- Example of weekly check
IF EXISTS (SELECT 1 FROM your_table WHERE performance_degraded) THEN
RAISE NOTICE 'Performance issue detected';
END IF;
END;
$$;
$$);
```

### Automate data refresh for materialized views

You can keep materialized views up to date ensuring that they reflect recent data changes.

The following example schedules a job to refresh the `your_materialized_view` every hour.

```sql
SELECT cron.schedule('refresh_materialized_view', '0 * * * *', 'REFRESH MATERIALIZED VIEW your_materialized_view');
```

### Cleanup old logs or temporary data

You can periodically clean up old logs or temporary data to free up space.

The following example schedules a job to delete logs older than 30 days every day at 2AM.

```sql
SELECT cron.schedule('cleanup_old_logs', '0 2 * * *', $$
DO $$
BEGIN
DELETE FROM logs WHERE log_date < NOW() - INTERVAL '30 days';
END;
$$);
```
