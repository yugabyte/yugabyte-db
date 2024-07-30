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

YugabyteDB is a distributed database that operates on multiple nodes. pg_cron only runs on one of these nodes called the pg_cron leader. Only the pg_cron leader schedules and runs the cron jobs. The queries executed by the job will take advantage of all available resources in the cluster. The leader is automatically elected by the database. If the pg_cron leader node fails, another node is automatically elected as the new leader to ensure it is highly available.

## Installation

### 1. Enable the pg_cron feature
To enable pg_cron, add `enable_pg_cron` to the `allowed_preview_flags_csv` flag and then set the `enable_pg_cron` flag to true on both YB-Master and TB-TServer.

For example, to create a single-node cluster with pg_cron enabled using [yugabyted](../../../../reference/configuration/yugabyted/), use the following  command:
```sh
./bin/yugabyted start --master_flags "enable_pg_cron=true,allowed_preview_flags_csv=enable_pg_cron" --tserver_flags "enable_pg_cron=true,allowed_preview_flags_csv=enable_pg_cron" --ui false
```

### 2. Set the pg_cron database (optional)
By default, pg_cron runs on the `yugabyte` database. You can change this by setting the `ysql_cron_database_name` flag to your desired database name.
{{< note >}}
- The database can be created after setting the flag.
- In order to change the database after the extension is created, you must drop the extension before changing the flag.
{{< /note >}}


### 3. Create and use the pg_cron extension
Create the extension as superuser on the cron database.
You can grant access to other users to use the extension.

```sql
CREATE EXTENSION pg_cron;
-- optionally, grant usage to regular users:
GRANT USAGE ON SCHEMA cron TO elephant;
```

For information on how to schedule jobs, refer to the [README](https://github.com/yugabyte/yugabyte-db/blob/master/src/postgres/third-party-extensions/pg_cron/README.md).

{{< note title="Note" >}}
- It may take up to 60 seconds for job changes to get reflected on the pg_cron leader.
- When a new pg_cron leader is elected, no jobs are run for the first minute. Any job that was inflight on the failed node will not be retied, as their outcome is not known.
{{< /note >}}