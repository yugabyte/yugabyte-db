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

## pg_cron in YugabyteDB

YugabyteDB supports all features of the pg_cron extension.

YugabyteDB is a distributed database that operates on multiple nodes. pg_cron only runs on one of these nodes called the pg_cron leader. Only the pg_cron leader schedules and runs the cron jobs. The queries executed by the job will take advantage of all available resources in the cluster. The database will automatically elect the leader node and ensure pg_cron is highly available and tolerant to node failures.

### Enable and configure
To enable pg_cron, add `enable_pg_cron` to the `allowed_preview_flags_csv` flag and set the `enable_pg_cron` flag to true on all YB-Masters and YB-TServers.

The pg_cron extension is installed on only one database, which stores the extensions data. The default cron database is `yugabyte`. You can change it by setting the `ysql_cron_database_name` flag on all YB-TServers.

For example, to create a single-node [yugabyted](../../../../reference/configuration/yugabyted/) cluster with pg_cron on database 'db1', use the following  command:
```sh
./bin/yugabyted start --master_flags "allowed_preview_flags_csv={enable_pg_cron},enable_pg_cron=true" --tserver_flags "allowed_preview_flags_csv={enable_pg_cron},enable_pg_cron=true,ysql_cron_database_name=db1" --ui false
```

{{< note title="Note" >}}
- The database can be created after setting the flag.
- If you need to run jobs in multiple databases, use `cron.schedule_in_database()`.
- In order to change the database after the extension is created, you must first drop the extension and then change the flag value.
{{< /note >}}


### Create and use
Create the extension as superuser on the cron database.
You can then grant access to other users to use the extension.

```sql
CREATE EXTENSION pg_cron;
```

You can grant access to other users to use the extension. For example:

```sql
GRANT USAGE ON SCHEMA cron TO elephant;
```

```sql
-- Call a stored procedure every 5 seconds
SELECT cron.schedule('process-updates', '5 seconds', 'CALL process_updates()');
```

For information on how to schedule jobs, refer to the [README](https://github.com/yugabyte/yugabyte-db/blob/master/src/postgres/third-party-extensions/pg_cron/README.md).

{{< note title="Note" >}}
- It may take up to 60 seconds for job changes to get picked up by the pg_cron leader.
- When a new pg_cron leader node is elected, no jobs are run for the first minute. Any job that were in flight on the failed node will not be retied, as their outcome is not known.
{{< /note >}}