---
title: TTL for data expiration in YCQL
headerTitle: TTL for data expiration
linkTitle: 9. TTL for data expiration
description: Learn how to use TTL for data expiration in YCQL.
menu:
  v2.14:
    identifier: ttl-data-expiration-ycql
    parent: learn
    weight: 581
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../ttl-data-expiration-ysql/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="../ttl-data-expiration-ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>

In YCQL there are two types of TTL, the table level TTL and column level TTL. The column level TTLs are stored
with the value of the column. The table level TTL is not stored in DocDB (it is stored
in yb-master system catalog as part of the table’s schema). If no TTL is present at the column’s value,
the table level TTL acts as the default value.

Furthermore, YCQL has a distinction between rows created using Insert vs. Update. We keep track of
this difference (and row level TTLs) using a "liveness column", a special system column invisible to
the user. It is added for inserts, but not updates: making sure the row is present even if all
non-primary key columns are deleted only in the case of inserts.

## Table level TTL

YCQL allows the TTL property to be specified at the table level.
In this case, you do not store the TTL on a per KV basis in DocDB; but the TTL is implicitly enforced
on reads as well as during compactions (to reclaim space).
Table level TTL can be defined with `default_time_to_live` [property](../../../api/ycql/ddl_create_table/#table-properties-1).

Below, you will look at how the row-level TTL is achieved in detail.

## Row level TTL

YCQL allows the TTL property to be specified at the level of each INSERT/UPDATE operation.
Row level TTL expires the whole row. The value is specified at insert/update time with `USING TTL` clause.
In such cases, the TTL is stored as part of the DocDB value. A simple query would be:

```sql
INSERT INTO pageviews(path) VALUES ('/index') USING TTL 10;
SELECT * FROM pageviews;

 path   | views
--------+-------
 /index |  null

(1 rows)
```

After 10 seconds, the row is expired:

```sql
SELECT * FROM pageviews;

 path | views
------+-------

(0 rows)
```

## Column level TTL

YCQL also allows to set column level TTL. In such cases, the TTL is stored as part of the DocDB column value.
But you can set it only when updating the column:

```sql
INSERT INTO pageviews(path,views) VALUES ('/index', 10);

SELECT * FROM pageviews;

 path   | views
--------+-------
 /index |  10

(1 rows)

UPDATE pageviews USING TTL 10 SET views=10 WHERE path='/index';
```

After 10 seconds, querying for the rows the `views` column will return `NULL` but notice that the row still exists:

```sql
SELECT * FROM pageviews;

 path   | views
--------+-------
 /index |  null

(1 rows)
```

## Efficient data expiration for TTL

YCQL includes a file expiration feature optimized for workloads that primarily rely on table-level TTL settings (or have consistent row- or column-level TTL values). This feature reduces both CPU usage and space amplification, particularly for time series workloads that frequently use table-level TTL to hold a dataset to a specific size. This is accomplished by organizing data into files by time, similar to Cassandra's time window compaction strategy.

This feature is available in 2.6.10+, 2.8.2+, and 2.12.1+.

### Configuring for new YCQL datasets

If configuring a new YCQL database for time series datasets and using a default time to live, we recommend the following TServer flag configurations:

##### --tablet_enable_ttl_file_filter = true

Enables expired files to be directly deleted, rather than relying on garbage collection during compaction.

##### --rocksdb_max_file_size_for_compaction = \[the amount of data to be deleted at once, in bytes\]

The value for this flag depends on how much data is expected to be deleted at once. For example, if a table's TTL is 90 days, it might be desireable to delete three days' worth of data at once. In this case, `rocksdb_max_file_size_for_compaction` should be set to the amount of data expected to be generated in 3 days. Files over this size will be excluded from normal compactions, leading to CPU gains.

Note that there is some tradeoff here between the number of files created and read perforance. A reasonable rule of thumb is to configure the flag such that 30 to 50 files store the full dataset (for example, 90 days divided by 3 days is 30 files). CPU benefits of using this feature should more than make up for any read performance loss.

##### --sst_files_soft_limit = \[number of expected files at steady state + 20\]
##### --sst_files_hard_limit = \[number of expected files at steady state + 40\]

The value of these flags depends on the number of files expected to hold the full dataset at steady state. These flags throttle writes to YCQL if the number of files per tablet exceed their value. Thus, they need to be set in a way that will accomodate the number of files expected in the dataset's steady state. In the example above, 30 files should hold 90 days worth of data. In this case, *sst_files_soft_limit* would be set to 50, and *sst_files_hard_limit* set to 70.

In some fresh dataset cases, new data will be backfilled into the database before the application is turned on. This backfilled data may have a value-level TTL associated with it that is significantly lower than the `default_time_to_live` property on the table, with the desired effect being that this data be removed earlier than the table TTL would allow. By default, such data will *not* expire early. However, the `file_expiration_value_ttl_overrides_table_ttl` flag can be used to ignore table TTL and expire solely based on value TTL.

{{< warning title="Warning" >}}
When using the `file_expiration_value_ttl_overrides_table_ttl` flag, be sure to set the flag back to `false` *before* all data with value-level TTL (for example, backfilled data) has fully expired. Failing to do so can result in unexpected loss of data. For example, if the `default_time_to_live` is 90 days, and data has been backfilled with value-level TTL from 1 day to 89 days, it is important that the `file_expiration_value_ttl_overrides_table_ttl` flag be set back to `false` within 89 days of data ingestion to avoid data loss.
{{< /warning >}}

### Configuring for existing YCQL datasets

To convert existing YCQL tables to ones configured for file expiration, the same TServer flag values as above can be used. However, expect a **temporary 2x space amplification** of the data should be expected in this case. This amplification happens because the existing file structure will have kept most data in a single large file, and that file will now be excluded from compactions going forward. Thus, this file will be unchanged until its contents has entirely expired, roughly *TTL amount of time* after the file expiration feature was configured.

Additionally, if data files were created with YugabyteDB versions below 2.6.6 or 2.8.1, files may lack the necessary metadata to be expired naturally. The `file_expiration_ignore_value_ttl` flag can be set to `true` to ignore the missing metadata. This will ignore the row- and column-level TTL metadata, expiring files purely based on the table's `default_time_to_live`.

{{< warning title="Warning" >}}
To prevent early data deletion, it is very important that in these cases, the `default_time_to_live` for any tables with TTL should be set to greater than or equal to the largest value-level TTL contained within those tables. It is recommended that once the files lacking the metadata have been removed, the `file_expiration_ignore_value_ttl` flag be set back to `false` (no restart required).
{{< /warning >}}

### Best usage and troubleshooting

* The file expiration feature is only enabled for tables with a default time to live. Even applications that explicitly set TTL on insert should be configured with a default time to live.
* The file expiration feature assumes that data arrives in rough chronological order relative to its expected expiration time. The feature is safe to use if this assumption is not met, but will be significantly less effective.
* Files are expired in a conservative manner, only being deleted after every data item it holds has completely expired. If a file has both a table-level TTL and column-level TTL, the later of the two is used in determining expiration.
* In situations in which a universe was created on a version older than 2.6.6 or 2.8.1, files may not contain the necessary metadata for file expiration. Similarly, if a data item is inserted with an unreasonably high TTL (or no TTL), the file expiration feature will stop being able to garbage collect data. In these cases, it may become necessary to set the `file_expiration_ignore_value_ttl` flag to `true`. NOTE: setting this flag to true can lead to unwanted loss of data. See [File expiration based on TTL flags](../../../reference/configuration/yb-tserver/#file-expiration-based-on-ttl-flags).
* If backfilling data into a table using a column TTL lower than the default TTL, it should be expected that this data will not expire until the table's default TTL has been exceeded. This can be circumvented by setting the `file_expiration_value_ttl_overrides_table_ttl` flag to `true`. NOTE: setting this flag to true can lead to unwanted loss of data. See [File expiration based on TTL flags](../../../reference/configuration/yb-tserver/#file-expiration-based-on-ttl-flags).

## TTL related commands & functions

There are several ways to work with TTL:

1. Table level TTL with [`default_time_to_live`](../../../api/ycql/ddl_create_table/#table-properties-1) property.
2. [Expiring rows with TTL](../../../api/ycql/dml_insert/#insert-a-row-with-expiration-time-using-the-using-ttl-clause).
3. [`TTL` function](../../../api/ycql/expr_fcall/#ttl-function) to return number of seconds until expiration.
4. [`WriteTime` function](../../../api/ycql/expr_fcall/#writetime-function) returns timestamp when row/column was inserted.
5. [Update row/column TTL](../../../api/ycql/dml_update/#using-clause) to update the TTL of a row or column.
6. [TServer flags related to TTL](../../../reference/configuration/yb-tserver/#file-expiration-based-on-ttl-flags) to configure the TServer for file expiration based on TTL.
