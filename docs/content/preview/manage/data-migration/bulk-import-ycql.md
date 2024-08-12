---
title: Bulk import YCQL
headerTitle: Import data
linkTitle: Import data
description: Import data from Apache Cassandra to YugabyteDB.
badges: ycql
aliases:
  - /preview/manage/data-migration/ycql/bulk-import/
menu:
  preview:
    identifier: manage-bulk-import-ycql
    parent: manage-bulk-import-export
    weight: 730
type: docs
---

{{<api-tabs>}}

Depending on the data volume imported, various bulk import tools can be used to load data into YugabyteDB. This page documents bulk import for YugabyteDB's [Cassandra-compatible YCQL API](../../../api/ycql/).

You should first export data from existing Apache Cassandra and MySQL tables. Next, you can import the data using the various bulk load options supported by YugabyteDB.

The import process is illustrated as follows, using a generic IoT time series data use case as a running example.

## Create destination table

Following is the schema of the destination YugabyteDB table:

```sql
CREATE KEYSPACE example;
USE EXAMPLE;

CREATE TABLE SensorData (
  customer_name text,
  device_id int,
  ts timestamp,
  sensor_data map<text, double>,
  PRIMARY KEY((customer_name, device_id), ts)
);
```

## Prepare source data

Prepare a comma-separated values (CSV) file where each row of entries matches the column types declared in the table schema provided in [Create destination table](#create-destination-table). Concretely, each CSV must be a valid Cassandra Query Language (CQL) literal for its corresponding type, except for the top-level quotes (for example, use foo rather than 'foo' for strings).

### Generate sample data

If you do not have the data already available in a database table, you can create sample data for the import using the following example:

```sh
#!/bin/bash

# Usage: ./generate_data.sh <number_of_rows> <output_filename>
# Example ./generate_data.sh 1000 sample.csv

if [ "$#" -ne 2 ]
then
  echo "Usage: ./generate_data.sh <number_of_rows> <output_filename>"
  Echo "Example ./generate_data.sh 1000 sample.csv"
  exit 1
fi

> $2 # clearing file
for i in $(seq 1 "$1"); do
  timestamp=$(date -u +"%Y-%m-%dT%H:%M:%S.000000+0000")
  echo "customer${i},${i},${timestamp},\"{temp:${i}, humidity:${i}}\"" >> "$2"
done
```

```output
customer1,1,2017-11-11 12:32:1.000000+0000,"{temp:1, humidity:1}"
customer2,2,2017-11-11 12:32:2.000000+0000,"{temp:2, humidity:2}"
customer3,0,2017-11-11 12:32:3.000000+0000,"{temp:3, humidity:3}"
customer4,1,2017-11-11 12:32:4.000000+0000,"{temp:4, humidity:4}"
customer5,2,2017-11-11 12:32:5.000000+0000,"{temp:5, humidity:5}"
customer6,0,2017-11-11 12:32:6.000000+0000,"{temp:6, humidity:6}"
```

### Export from Apache Cassandra

If you already have the data in an Apache Cassandra table, then use the following command to create a CSV file with that data:

```sql
ycqlsh> COPY example.SensorData TO '/path/to/sample.csv';
```

### Export from MySQL

If you already have the data in a MySQL table named `SensorData`, then use the following command to create a CSV file with that data:

```sql
SELECT customer_name, device_id, ts, sensor_data
FROM SensorData
INTO OUTFILE '/path/to/sample.csv' FIELDS TERMINATED BY ',';
```

## Import data

The import data instructions are organized by the size of the input datasets, ranging from small (megabytes of data) to larger datasets (gigabytes of data).

### Small datasets

Cassandra's CQL shell provides the [`COPY FROM`](../../../admin/ycqlsh/#copy-from) command, which allows importing data from CSV files:

```sql
ycqlsh> COPY example.SensorData FROM '/path/to/sample.csv';
```

By default, `COPY` exports timestamps in the `yyyy-MM-dd HH:mm:ss.SSSZ` format.

See also [`COPY TO`](../../../admin/ycqlsh/#copy-to) .

### Large datasets

[`cassandra-loader`](https://github.com/brianmhess/cassandra-loader) is a general-purpose bulk loader for CQL that supports various types of delimited files (particularly CSV files). For details, review the README of the [YugabyteDB cassandra-loader fork](https://github.com/yugabyte/cassandra-loader/). Note that `cassandra-loader` requires quotes for collection types (for example, "[1,2,3]" rather than [1,2,3] for lists).

#### Install cassandra-loader

You can install `cassandra-loader` as follows:

```sh
wget https://github.com/yugabyte/cassandra-loader/releases/download/<latest-version>/cassandra-loader
```

```sh
chmod a+x cassandra-loader
```

#### Run cassandra-loader

Run `cassandra-loader` using the following command:

```sh
time ./cassandra-loader \
  -dateFormat 'yyyy-MM-dd HH:mm:ss.SSSSSSX' \
  -f sample.csv \
  -host <clusterNodeIP> \
  -schema "example.SensorData(customer_name, device_id, ts, sensor_data)"
```

For additional options, refer to [cassandra-loader options](https://github.com/yugabyte/cassandra-loader#options).

## Verify migration

After the data and schema have been migrated, follow the steps in [Verify migration](../verify-migration-ycql) to ensure the migration was successful.
