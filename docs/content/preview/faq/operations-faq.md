---
title: FAQs about operating YugabyteDB clusters
headerTitle: Operations FAQ
linkTitle: Operations FAQ
description: Answers to common questions about operating YugabyteDB clusters
menu:
  preview_faq:
    identifier: faq-operations
    parent: faq
    weight: 50
type: docs
unversioned: true
showRightNav: false
---

## Do YugabyteDB clusters need an external load balancer?

For YSQL, you should use a YugabyteDB smart driver. YugabyteDB smart drivers automatically balance connections to the database and eliminate the need for an external load balancer. If you are not using a smart driver, you will need an external load balancer.

{{<lead link="../../drivers-orms/smart-drivers/">}}
YugabyteDB smart drivers for YSQL
{{</lead>}}

For YCQL, YugabyteDB provides automatic load balancing.

[YugabyteDB Aeon](../../yugabyte-cloud/) clusters automatically use the uniform load balancing provided by the cloud provider where the cluster is provisioned. YugabyteDB Aeon creates an external load balancer to distribute the connection load across the nodes in a particular region. For multi-region clusters, each region has its own external load balancer. For regular connections, you need to connect to the region of choice, and application connections are then uniformly distributed across the region without the need for any special coding.

{{<lead link="../../drivers-orms/smart-drivers/#using-smart-drivers-with-yugabytedb-aeon">}}
Connection load balancing in YugabyteDB Aeon
{{</lead>}}

### Using GCP load balancers

To configure a YugabyteDB universe deployed on GCP to use GCP-provided load balancers, you must set the [--pgsql_proxy_bind_address 0.0.0.0:5433](../../reference/configuration/yb-tserver/#pgsql-proxy-bind-address) and [--cql_proxy_bind_address 0.0.0.0:9042](../../reference/configuration/yb-tserver/#cql-proxy-bind-address) flags.

{{<lead link="../../yugabyte-platform/manage-deployments/edit-config-flags/">}}
Edit configuration flags
{{</lead>}}

## Can write ahead log (WAL) files be cleaned up or reduced in size?

For most YugabyteDB deployments, you should not need to adjust the configuration flags for the write ahead log (WAL). While your data size is small and growing, the WAL files may seem to be much larger, but over time, the WAL files should reach their steady state while the data size continues to grow and become larger than the WAL files.

WAL files are per tablet and the retention policy is managed by the following two yb-tserver configuration flags:

- [`--log_min_segments_to_retain`](../../reference/configuration/yb-tserver/#log-min-segments-to-retain)
- [`--log_min_seconds_to_retain`](../../reference/configuration/yb-tserver/#log-min-seconds-to-retain)

Also, the following yb-tserver configuration flag is a factor in the size of each WAL file before it is rolled into a new one:

- [`--log_segment_size_mb`](../../reference/configuration/yb-tserver/#log-segment-size-mb) – default is `64`.

## How do I determine the size of a YSQL database?

YugabyteDB doesn't currently support the `pg_database_size` function. Instead, use a custom function based on `pg_table_size` to calculate the size of the database.

```sql
CREATE OR REPLACE FUNCTION yb_pg_database_size()
  RETURNS BIGINT
AS
$$
DECLARE
  sql_statement RECORD;
  ObjectSize BIGINT := 0;
  DataBaseSize BIGINT := 0;
BEGIN
  -- Tables
  FOR sql_statement IN
    SELECT 'SELECT pg_table_size(''' || schemaname || '.' || tablename || ''');' AS ddl FROM pg_tables WHERE schemaname NOT IN ('information_schema', 'pg_catalog')
  LOOP
    EXECUTE sql_statement.ddl INTO ObjectSize;
    DataBaseSize := DataBaseSize + ObjectSize;
  END LOOP;

  -- Indexes
  FOR sql_statement IN
  SELECT 'SELECT pg_table_size(''' || n.nspname || '.' || c.relname || ''');' AS ddl FROM pg_catalog.pg_class c LEFT JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace LEFT JOIN pg_catalog.pg_index i ON i.indexrelid = c.oid WHERE n.nspname NOT IN ('pg_catalog', 'information_schema') AND c.relkind IN ('i','I','') AND NOT i.indisprimary
  LOOP
    EXECUTE sql_statement.ddl INTO ObjectSize;
    DataBaseSize := DataBaseSize + ObjectSize;
  END LOOP;

  RETURN DataBaseSize;

END;
$$
LANGUAGE plpgsql;
```

Now, when you run,

```sql
SELECT yb_pg_database_size() as size_bytes, yb_pg_database_size()/1048576 as size_mb;
```

You should see output like the following:

```caddyfile{.nocopy}
 size_bytes | size_mb
------------+---------
   80540743 |      76
```

{{<lead link="https://yugabytedb.tips/display-ysql-database-size/">}}
For more information, see [Display YSQL Database size](https://yugabytedb.tips/display-ysql-database-size/)
{{</lead>}}

## How can I create a user in YSQL with a password that expires after a specific time interval?

You can create a user with a password that expires after a set time using the `VALID UNTIL` clause. The following example sets the expiration time 4 hours from the current time:

```plpgsql
DO $$
DECLARE time TIMESTAMP := now() + INTERVAL '4 HOURS';
BEGIN 
  EXECUTE format(
    'CREATE USER "John" WITH PASSWORD ''secure_password'' VALID UNTIL ''%s'';', 
    time
  ); 
END
$$;
```

To verify the password expiration time, run the following query:

```plpgsql
SELECT now(), valuntil, valuntil - now() AS diff 
FROM pg_user 
WHERE usename = 'John';
```

```output
             now              |        valuntil        |      diff       
------------------------------+------------------------+----------------
 2025-01-23 17:16:22.82708+00 | 2025-01-23 21:16:21+00 | 03:59:58.17292
(1 row)
```

This confirms that the password for `John` will expire in approximately 4 hours.
