

## Overview

## Datatypes

### timeuuid

### built-in functions

## Creating table

## Batch insert data

## Querying data

### Reading the latest datapoint

### Select the latest k entries

### Time range queries

## Sample Java Application

You can find a working example of using transactions with YugaByte in our [sample applications](/quick-start/run-sample-apps/). Here is how you can try out this sample application.

```sh
 - CassandraTimeseries :
   -------------------
    Sample timeseries/IoT app built on CQL. The app models 100 users, each of
    whom own 5-10 devices. Each device emits 5-10 metrics per second. The data is
    written into the 'ts_metrics_raw' table, which retains data for one day. Note that
    the number of metrics written is a lot more than the number of metrics read as is
    typical in such workloads, and the payload size for each write is 100 bytes. Every
    read query fetches the last 1-3 hours of metrics for a user's device.

    Usage:
      java -jar yb-sample-apps.jar \
      --workload CassandraTimeseries \
      --nodes 127.0.0.1:9042

    Other options (with default values):
      [ --num_threads_read 1 ]
      [ --num_threads_write 16 ]
      [ --num_users 100 ]
      [ --min_nodes_per_user 5 ]
      [ --max_nodes_per_user 10 ]
      [ --min_metrics_count 5 ]
      [ --max_metrics_count 10 ]
      [ --data_emit_rate_millis 1000 ]
      [ --table_ttl_seconds 86400 ]
```


Browse the [Java source code for the batch application](https://github.com/YugaByte/yugabyte-db/blob/master/java/yb-loadtester/src/main/java/com/yugabyte/sample/apps/CassandraTimeseries.java) to see how everything fits together.
