---
title: Bulk import YCQL
headerTitle: Bulk import for YCQL
linkTitle: Bulk import
description: Import data from Apache Cassandra to YugabyteDB.
menu:
  v2.20:
    identifier: manage-bulk-import-ycql
    parent: manage-bulk-import-export
    weight: 707
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../bulk-import-ysql/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li >
    <a href="../bulk-import-ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

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

Cassandra's CQL shell provides the [`COPY FROM`](../../../api/ycqlsh/#copy-from) command, which allows importing data from CSV files:

```sql
ycqlsh> COPY example.SensorData FROM '/path/to/sample.csv';
```

By default, `COPY` exports timestamps in the `yyyy-MM-dd HH:mm:ss.SSSZ` format.

See also [`COPY TO`](../../../api/ycqlsh/#copy-to) .

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

## Verify a migration

To ensure that the migration was successful, you can perform a number of steps.

### Verify database objects

- Verify that all the tables and indexes have been created in YugabyteDB.
- Ensure that triggers and constraints are migrated and are working as expected.

### Run count query in YCQL

In YCQL, the `count()` query can be executed using the [ycrc](https://github.com/yugabyte/yb-tools/tree/main/ycrc) tool.

The tool uses the exposed `partition_hash` function in order to execute smaller, more manageable queries which are individually less resource intensive and tend to not time out.

Set up and run the ycrc tool using the following steps:

1. Download the ycrc tool by compiling the source from the GitHub repository.

1. Run the following command to confirm that the ycrc tool is working:

    ```sh
    ./ycrc --help
    ```

    ```output
    YCQL Row Count (ycrc) parallelizes counting the number of rows in a table for YugabyteDB CQL, allowing count(*) on tables that otherwise would fail with query timeouts

    Usage:

     ycrc <keyspace> [flags]

    Flags:

     -d, --debug  Verbose logging
     -h, --help  help for ycrc
     -c, --hosts strings  Cluster to connect to (default [127.0.0.1])
     -p, --parallel int  Number of concurrent tasks (default 16)
     --password string  user password
     -s, --scale int  Scaling factor of tasks per table, an int between 1 and 10 (default 6)
     --sslca string  SSL root ca path
     --sslcert string  SSL cert path
     --sslkey string  SSL key path
     --tables strings  List of tables inside of the keyspace - default to all
     -t, --timeout int  Timeout of a single query, in ms (default 1500)
     -u, --user string  database user (default "cassandra")
     --verify  Strictly verify SSL host (off by default)
     -v, --version  version for ycrc

    ```

1. Run the ycrc tool to count the rows in a given keyspace using a command similar to the following:

    ```cql
    ./ycrc -c 127.0.0.1 example
    ```

    ```output
    Checking table row counts for keyspace: example
    Checking row counts for: example.sensordata
    Partitioning columns for example.sensordata:(customer_name,device_id)

    Performing 4096 checks for example.sensordata with 16 parallel tasks

    Total time: 261 ms

    ==========
    Total Row Count example.sensordata = 60
    Checking row counts for: example.emp
    Partitioning columns for example.emp:(emp_id)

    Performing 4096 checks for example.emp with 16 parallel tasks

    Total time: 250 ms

    ==========
    Total Row Count example.emp = 3
    ```

    The following is an example with additional flags on the keyspace `example`:

    ```sh
    ./ycrc example \
          -c 127.0.0.1 \                         # Cluster to connect to (default [127.0.0.1])
          -u test \                              # database user (default "cassandra")
          --verify --password xxx \              # user password
          --tables sensordata \                  # List of tables inside of the keyspace - default to all
          -s 7 \                                 # Scaling factor of tasks per table, an int between 1 and 10 (default 6)
          -p 32 \                                # Number of concurrent tasks (default 16)
          -t 3000 \                              # Timeout of a single query, in ms (default 1500)
          --sslca /opt/yugabyte/certs/tests.crt  # This flag needs to specified if client to node authentication is enabled.
    ```
