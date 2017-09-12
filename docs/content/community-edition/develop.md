---
date: 2016-03-09T20:08:11+01:00
title: Community Edition - Develop
weight: 20
---

## Review sample apps

The YugaByte DB package ships with multiple Apache Cassandra Query Language (CQL) and Redis sample apps that you can use as a starting point for developing your own application. The CQL apps available are `CassandraHelloWorld`, `CassandraKeyValue`, `CassandraStockTicker`, `CassandraTimeseries`, `CassandraUserId`, `CassandraSparkWordCount`. The Redis apps available are `RedisKeyValue` and `RedisPipelinedKeyValue`.

You can get help on all these apps by simply running the command below.

```sh
$ java -jar ./java/yb-sample-apps.jar --help
```

The source code for these apps is also available in the same directory as the compiled jar. Run the command below to expand the source jar files into `com/yugabyte/sample/apps` path of the current directory.

```sh
$ jar xf ./java/yb-sample-apps-sources.jar
```

## CQL sample apps

### CassandraHelloWorld

A very simple hello world app. The app writes one employee row into an 'Employee' table

```sh
$ java -jar ./java/yb-sample-apps.jar --workload CassandraHelloWorld --nodes localhost:9042
```

### CassandraKeyValue

Sample key-value app that writes out 1M unique string keys each with a string value. There are multiple readers and writers that update these keys and read them indefinitely. Note that the number of reads and writes to perform can be specified as a parameter.

```sh
$ java -jar ./java/yb-sample-apps.jar --workload CassandraKeyValue --nodes localhost:9042
```

Other options (with default values):

```sh
[ --num_unique_keys 1000000 ]
[ --num_reads -1 ]
[ --num_writes -1 ]
[ --value_size 0 ]
[ --num_threads_read 24 ]
[ --num_threads_write 2 ]
[ --table_ttl_seconds -1 ]
```

### CassandraStockTicker

Sample stock ticker app that models 10,000 stock tickers each of which emits quote data every second. The raw data is written into the 'stock_ticker_raw' table, which retains data for one day. The 'stock_ticker_1min' table models downsampled ticker data, is written to once a minute and retains data for 60 days. Every read query gets the latest value of the stock symbol from the 'stock_ticker_raw' table.

```sh
$ java -jar ./java/yb-sample-apps.jar --workload CassandraStockTicker --nodes localhost:9042
```

Other options (with default values):

```sh
[ --num_threads_read 32 ]
[ --num_threads_write 4 ]
[ --num_ticker_symbols 10000 ]
[ --data_emit_rate_millis 1000 ]
[ --table_ttl_seconds 86400 ]
```

### CassandraTimeseries

Sample timeseries/IoT app that models 100 users, each of whom own 5-10 devices. Each device emits 5-10 metrics per second. The data is written into the 'ts_metrics_raw' table, which retains data for one day. Note that the number of metrics written is a lot more than the number of metrics read as is typical in such workloads, and the payload size for each write is 100 bytes. Every read query fetches the last 1-3 hours of metrics for a user's device.

```sh
$ java -jar ./java/yb-sample-apps.jar --workload CassandraTimeseries --nodes localhost:9042
```

Other options (with default values):

```sh
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

### CassandraUserId

Sample user id app that writes out 1M unique user ids each with a string password. There are multiple readers and writers that update these user ids and passwords them indefinitely. Note that the number of reads and writes to perform can be specified as a parameter.

```sh
$ java -jar ./java/yb-sample-apps.jar --workload CassandraUserId --nodes localhost:9042
```

Other options (with default values):

```sh
[ --num_unique_keys 1000000 ]
[ --num_reads -1 ]
[ --num_writes -1 ]
[ --value_size 100 ]
[ --num_threads_read 1 ]
[ --num_threads_write 16 ]
[ --table_ttl_seconds 86400 ]
```

### CassandraSparkWordCount

Simple Apache Spark word count app that reads from an input table or file to compute  word count and saves results in an output table. Input source is either input_file or input_table. If none is given a sample CQL table lines is created and used as input.

```sh
$ java -jar ./java/yb-sample-apps.jar --workload CassandraSparkWordCount --nodes localhost:9042
```

Other options (with default values):

```sh
[ --num_threads_write 2 ]
[ --wordcount_output_table wordcounts ]
[ --wordcount_input_file <path to input file> ]
[ --wordcount_input_table <table name> ]
```

## Redis sample apps

### RedisKeyValue

Sample key-value Redis app that writes out 1M unique string keys each with a string value. There are multiple readers and writers that update these keys and read them indefinitely. Note that the number of reads and writes to perform can be specified as a parameter.

```sh
$ java -jar ./java/yb-sample-apps.jar --workload RedisKeyValue --nodes localhost:6379
```

Other options (with default values):

```sh
[ --num_unique_keys 1000000 ]
[ --num_reads -1 ]
[ --num_writes -1 ]
[ --num_threads_read 32 ]
[ --num_threads_write 2 ]
[ --nouuid]
```

The `--nouuid` option ensures that the keys are not based on an uuid for every run. The keys created with this option will be `key:1`, `key:2`, `key:3` and so on. The values corresponding to the keys would be `val:1`, `val:2`, `val:3` and so on.

### RedisPipelinedKeyValue

Sample key-value Redis app writes out 1M unique string keys each with a string value. There are multiple readers and writers that update these keys and read them indefinitely. Note that the number of reads and writes to perform can be specified as a parameter.

```sh
$ java -jar ./java/yb-sample-apps.jar --workload RedisPipelinedKeyValue --nodes localhost:6379
```

```sh
[ --num_unique_keys 1000000 ]
[ --num_reads -1 ]
[ --num_writes -1 ]
[ --num_threads_read 32 ]
[ --num_threads_write 2 ]
[ --pipeline_length 1 ]
```