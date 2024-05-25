---
title: pg_cron extension
headerTitle: pg_cron extension
linkTitle: pg_cron
description: Using the pg_cron extension in YugabyteDB
menu:
  preview:
    identifier: extension-pgcron
    parent: pg-extensions
    weight: 20
type: docs
---

The [pg_cron](https://github.com/citusdata/pg_cron) extension provides a cron-based job scheduler that runs inside the database. It uses the same syntax as regular cron, but it allows you to schedule YSQL commands directly from the database. You can also use '[1-59] seconds' to schedule a job based on an interval.

## Enable pg_cron
This feature is {{<badge/tp>}}. You can enable the feature using the `enable_pg_cron` flag. As it is a preview flag, you have to add it to `allowed_preview_flags_csv`.

By default, pg_cron will run on the `yugabyte` database. You can change this by setting `ysql_cron_database_name` to your desired database name.

To create a single-node cluster with pg_cron using [yugabyted](../../../reference/configuration/yugabyted/), use the following  command:

```sh
./bin/yugabyted start --master_flags "enable_pg_cron=true,allowed_preview_flags_csv=enable_pg_cron" --tserver_flags "enable_pg_cron=true,allowed_preview_flags_csv=enable_pg_cron" --ui false
```


After the flags are set, run the following command on the cron database with superuser privileges. 

```sql
CREATE EXTENSION pg_cron;
```

For more information on how to schedule jobs refer to the [pg_cron readme](https://github.com/yugabyte/yugabyte-db/blob/master/src/postgres/third-party-extensions/pg_cron/README.md)
