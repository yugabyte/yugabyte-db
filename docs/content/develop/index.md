---
date: 2016-03-09T20:08:11+01:00
title: Develop
weight: 50
---

## Connect with cqlsh

[**cqlsh**](http://cassandra.apache.org/doc/latest/tools/cqlsh.html) is a command line shell for interacting with Cassandra through [CQL (the Cassandra Query Language)](http://cassandra.apache.org/doc/latest/cql/index.html). It is shipped with every Cassandra package, and can be found in the bin/ directory. cqlsh utilizes the Python CQL driver, and connects to the single node specified on the command line.

- Download and run cqlsh

```sh
$ docker run -it --rm quay.io/yugabyte/cqlsh $(docker inspect --format '{{ .NetworkSettings.IPAddress }}' yugabyte-db)
```

- Output will look similar to the following

```sh
Connected to local cluster at localhost:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cqlsh> 
```

- Run a cql command

```sh
cqlsh> describe keyspaces;

system_schema  system  default_keyspace

cqlsh> 

```

## Connect with redis-cli


## Run sample apps

YugaWare ships with sample apps for the most common use cases powered by YugaByte. You can see the instructions on how to run these apps by simply clicking on the **Apps** button in the Overview tab of the Universe detail page.

![Time Series Sample App](/images/time-series-sample-app.png)

Log into the YugaWare host machine.

```sh
# open a bash shell in the yugaware docker container
sudo docker exec -it yugaware bash

# run the time-series sample app
java -jar /opt/yugabyte/utils/yb-sample-apps.jar --workload CassandraTimeseries --nodes 10.151.22.132:9042,10.151.38.209:9042,10.151.50.1:9042
```

Other values of the `workload` param are `CassandraStockTicker`, `CassandraKeyValue`, `RedisKeyValue`


You will see the following output.

```sh
root@5b817c216fdd:/opt/yugabyte/yugaware# java -jar /opt/yugabyte/utils/yb-sample-apps.jar --workload CassandraTimeseries --nodes 172.20.0.13:9042,172.20.0.11:9042,172.20.0.12:9042
2017-06-27 16:25:35,426 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Using a randomly generated UUID : 8c23b82e-735f-4d74-aafe-8b107181d0f6
2017-06-27 16:25:35,448 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] App: CassandraTimeseries
2017-06-27 16:25:35,448 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Adding node: 172.20.0.13:9042
2017-06-27 16:25:35,449 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Adding node: 172.20.0.11:9042
2017-06-27 16:25:35,456 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Adding node: 172.20.0.12:9042
2017-06-27 16:25:35,456 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num reader threads: 1, num writer threads: 16
2017-06-27 16:25:35,457 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num unique keys to insert: 0
2017-06-27 16:25:35,457 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num keys to update: -1
2017-06-27 16:25:35,457 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num keys to read: -1
2017-06-27 16:25:35,457 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Value size: 100
2017-06-27 16:25:35,458 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Table TTL (secs): 86400
2017-06-27 16:25:37,803 [INFO|com.yugabyte.sample.apps.AppBase|AppBase] Ignoring exception dropping table: SQL error (yb/sql/ptree/process_context.cc:41): SQL Error (1.11): Table Not Found - Not found (yb/common/wire_protocol.cc:120): The table does not exist: table_name: "ts_metrics_raw"
namespace {
  name: "ybdemo_keyspace"
}

	ts_metrics_raw;
	^^^^^^^^^^^^^^
 (error -301)
2017-06-27 16:25:40,592 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 0.00 ops/sec (0.00 ms/op), 0 total ops  |  Write: 0.00 ops/sec (0.00 ms/op), 0 total ops  |  Uptime: 5134 ms | Verification: ON | 
2017-06-27 16:25:45,204 [INFO|com.yugabyte.sample.apps.AppBase|AppBase] Created a Cassandra table using query: [CREATE TABLE IF NOT EXISTS ts_metrics_raw (  user_id varchar, metric_id varchar, node_id varchar, ts timestamp, value varchar, primary key ((user_id, metric_id), node_id, ts)) WITH default_time_to_live = 86400;]
2017-06-27 16:25:45,593 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 0.00 ops/sec (0.00 ms/op), 0 total ops  |  Write: 0.00 ops/sec (0.00 ms/op), 0 total ops  |  Uptime: 10135 ms | Verification: ON | 
log4j:WARN No appenders could be found for logger (io.netty.util.ResourceLeakDetector).
log4j:WARN Please initialize the log4j system properly.
log4j:WARN See http://logging.apache.org/log4j/1.2/faq.html#noconfig for more info.
2017-06-27 16:25:50,594 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 69.59 ops/sec (13.10 ms/op), 348 total ops  |  Write: 696.90 ops/sec (23.73 ms/op), 3485 total ops  |  Uptime: 15136 ms | Verification: ON | 
2017-06-27 16:25:55,597 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 93.54 ops/sec (10.68 ms/op), 816 total ops  |  Write: 868.87 ops/sec (18.04 ms/op), 7832 total ops  |  Uptime: 20139 ms | Verification: ON | 
2017-06-27 16:26:00,598 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 102.39 ops/sec (9.74 ms/op), 1328 total ops  |  Write: 909.46 ops/sec (17.32 ms/op), 12380 total ops  |  Uptime: 25140 ms | Verification: ON | 
2017-06-27 16:26:05,600 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 100.76 ops/sec (9.92 ms/op), 1832 total ops  |  Write: 905.59 ops/sec (17.49 ms/op), 16910 total ops  |  Uptime: 30142 ms | Verification: ON | 
2017-06-27 16:26:10,601 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 109.77 ops/sec (9.09 ms/op), 2381 total ops  |  Write: 874.00 ops/sec (18.33 ms/op), 21281 total ops  |  Uptime: 35143 ms | Verification: ON | 

```

As you can see above, the Cassandra time-series sample app first creates a table `ts_metrics_raw` and then starts multiple writer and reader threads to generate the load.

- Verify using cqlsh

```sh
cqlsh> use default_keyspace;

cqlsh:default_keyspace> describe tables;

ts_metrics_raw

cqlsh:default_keyspace> describe ts_metrics_raw;

CREATE TABLE default_keyspace.ts_metrics_raw (
    user_id text,
    metric_id text,
    node_id text,
    ts timestamp,
    value text,
    PRIMARY KEY ((user_id, metric_id), node_id, ts)
) WITH CLUSTERING ORDER BY (node_id ASC, ts ASC);

cqlsh:default_keyspace> select * from ts_metrics_raw limit 10;

 user_id                                 | metric_id                 | node_id    | ts                              | value
-----------------------------------------+---------------------------+------------+---------------------------------+--------------------------
 91-98c6eeac-0d30-4bb6-bbab-c1d80a9d760b | metric-00000.yugabyte.com | node-00004 | 2017-04-26 23:34:32.000000+0000 | 1493249672000[B@3d7e092e
 91-98c6eeac-0d30-4bb6-bbab-c1d80a9d760b | metric-00000.yugabyte.com | node-00004 | 2017-04-26 23:34:33.000000+0000 | 1493249673000[B@55c0e5c2
 91-98c6eeac-0d30-4bb6-bbab-c1d80a9d760b | metric-00000.yugabyte.com | node-00005 | 2017-04-26 23:34:52.000000+0000 |  1493249692000[B@5dcabaf
 91-98c6eeac-0d30-4bb6-bbab-c1d80a9d760b | metric-00000.yugabyte.com | node-00006 | 2017-04-26 23:34:44.000000+0000 | 1493249684000[B@6b3429c8
 91-98c6eeac-0d30-4bb6-bbab-c1d80a9d760b | metric-00000.yugabyte.com | node-00006 | 2017-04-26 23:34:45.000000+0000 | 1493249685000[B@5dd2c16d
 91-98c6eeac-0d30-4bb6-bbab-c1d80a9d760b | metric-00000.yugabyte.com | node-00007 | 2017-04-26 23:34:42.000000+0000 | 1493249682000[B@293d7907
 91-98c6eeac-0d30-4bb6-bbab-c1d80a9d760b | metric-00000.yugabyte.com | node-00007 | 2017-04-26 23:34:43.000000+0000 | 1493249683000[B@43e0347b
 58-98c6eeac-0d30-4bb6-bbab-c1d80a9d760b | metric-00001.yugabyte.com | node-00000 | 2017-04-26 23:34:43.000000+0000 | 1493249683000[B@22d7b22b
 58-98c6eeac-0d30-4bb6-bbab-c1d80a9d760b | metric-00001.yugabyte.com | node-00004 | 2017-04-26 23:34:50.000000+0000 | 1493249690000[B@30a15573
 31-98c6eeac-0d30-4bb6-bbab-c1d80a9d760b | metric-00008.yugabyte.com | node-00000 | 2017-04-26 23:34:46.000000+0000 | 1493249686000[B@211e5e6e

(10 rows)
cqlsh:default_keyspace>

```

- Review the sample app source code


```sh
jar xf /tmp/yb-sample-apps-sources.jar
```
The above command puts the sample apps Java files in `com/yugabyte/sample/apps` in the current directory.


## Run your own app

### Create table

\<docs coming soon\>

### Write data to table

\<docs coming soon\>

### Read data from table

\<docs coming soon\>

