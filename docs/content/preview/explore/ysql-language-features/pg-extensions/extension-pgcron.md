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

## Examples

The following examples decribe various ways `pg_cron` can be used to automate and improve database management tasks. The tool can help maintain database performance, consistency, and reliability through scheduled jobs.

### Monitor and identify

Use [pg_stat_statements](../extension-pgstatstatements/) to capture statistics about queries, and schedule regular reports with `pg_cron` to summarize slow queries.

```sql
SELECT * FROM pg_stat_statements ORDER BY total_time DESC LIMIT 10;
```

### Deploy changes and validate

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

### Archive old data

You can move old or infrequently accessed data to an archive table to keep the main table performant.

This example schedules a job to archive old data from the `main_table` to the `archive_table` every month on the 1st at 1AM.

```sql
SELECT cron.schedule('monthly_archive', '0 1 1 * *', $$
DO $$
BEGIN
INSERT INTO archive_table SELECT * FROM main_table WHERE created_at < NOW() - INTERVAL '1 year';
DELETE FROM main_table WHERE created_at < NOW() - INTERVAL '1 year';
END;
$$);
```

### Automate data refresh for materialized views

You can keep materialized views up to date ensuring that they reflect recent data changes.

This example schedules a job to refresh the `your_materialized_view` every hour.

```sql
SELECT cron.schedule('refresh_materialized_view', '0 * * * *', 'REFRESH MATERIALIZED VIEW your_materialized_view');
```

### Cleanup old logs or temporary data

You can periodically clean up old logs or temporary data to free up space.

This example schedules a job to delete logs older than 30 days every day at 2AM.

```sql
SELECT cron.schedule('cleanup_old_logs', '0 2 * * *', $$
DO $$
BEGIN
DELETE FROM logs WHERE log_date < NOW() - INTERVAL '30 days';
END;
$$);
```

### Data synchronization between tables

You can synchronize data between tables ensuring consistency across different parts of the database.

This example schedules a job to synchronize data between `source_table` and `target_table` every 15 minutes.

```sql
SELECT cron.schedule('sync_tables', '*/15 * * * *', $$
DO $$
BEGIN
INSERT INTO target_table (col1, col2, col3)
SELECT col1, col2, col3
FROM source_table
WHERE NOT EXISTS (SELECT 1 FROM target_table WHERE target_table.id = source_table.id);
END;
$$);
```
