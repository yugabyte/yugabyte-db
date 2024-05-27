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

## Enable pg_cron

As pg_cron is in tech preview, you must first enable the feature by setting the `enable_pg_cron` preview flag to true, and adding it to the `allowed_preview_flags_csv` flag, for both YB-Master and TB-TServer.

For example, to create a single-node cluster with pg_cron enabled using [yugabyted](../../../../reference/configuration/yugabyted/), use the following  command:

```sh
./bin/yugabyted start --master_flags "enable_pg_cron=true,allowed_preview_flags_csv=enable_pg_cron" --tserver_flags "enable_pg_cron=true,allowed_preview_flags_csv=enable_pg_cron" --ui false
```

By default, pg_cron runs on the `yugabyte` database. You can change this by setting the `ysql_cron_database_name` flag to your desired database name.

After the flags are set, run the following command on the cron database with superuser privileges:

```sql
CREATE EXTENSION pg_cron;
```

For information on how to schedule jobs, refer to the [pg_cron](https://github.com/yugabyte/yugabyte-db/blob/master/src/postgres/third-party-extensions/pg_cron/README.md) documentation.
