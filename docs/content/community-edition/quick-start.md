---
date: 2016-03-09T00:11:02+01:00
title: Community Edition - Quick Start
weight: 15
---

 The easiest way to get started with the YugaByte Community Edition is to create a multi-node YugaByte **local cluster** on your laptop or desktop.

{{< note title="Note" >}}
Running local clusters is not recommended for production environments. You can either deploy the [Community Edition] (/community-edition/deploy/) manually on a set of instances or use the [Enterprise Edition](/enterprise-edition/deploy/) that automates all day-to-day operations including cluster administration across all major public clouds as well as on-premises datacenters.
{{< /note >}}

## Prerequisites

Operation systems supported for local clusters are

- Centos 7 or higher

## Download and install

### Download

Download the YugaByte DB package [here](https://s3-us-west-2.amazonaws.com/download.yugabyte.com/0.9.0.0/yugabyte.ce.0.9.0.0-b0.tar.gz). Thereafter, follow the instructions below.

### Install

On your localhost, execute the following commands.

```sh
$ mkdir ~/yugabyte
$ tar xvfz yugabyte.ce.<version>.tar.gz -C yugabyte
$ cd yugabyte
```

Run the **configure** script to ensure all dependencies get auto-installed. This script will also install a two libraries (`cyrus-sasl` and `cyrus-sasl-plain`) and will request for a sudo password in case you are not running the script as root.

```sh
$ ./bin/configure
```

## Create local cluster

### Create a 3 node cluster with replication factor 3 

We will use the [yb-ctl](/admin/yb-ctl) utility that has a set of pre-built commands to administer a local cluster. The default data directory used is `/tmp/yugabyte-local-cluster`. You can change this directory with the `--data_dir` option. Detailed output for the *create* command is available in [yb-ctl Reference](/admin/yb-ctl/#create-cluster).

```sh
$ ./bin/yb-ctl create
```

You can now check `/tmp/yugabyte-local-cluster` to see 2 directories `disk1` and `disk2` created. Inside each of these you will find the data for all the nodes in the respective `node-i` directores where `i` represents the `node_id` of the node.

### Check the status of the cluster.

```sh
$ ./bin/yb-ctl status
2017-09-06 22:53:40,871 INFO: Server is running: type=master, node_id=1, PID=28494, URL=127.0.0.1:7000
2017-09-06 22:53:40,876 INFO: Server is running: type=master, node_id=2, PID=28504, URL=127.0.0.1:7001
2017-09-06 22:53:40,881 INFO: Server is running: type=master, node_id=3, PID=28507, URL=127.0.0.1:7002
2017-09-06 22:53:40,885 INFO: Server is running: type=tserver, node_id=1, PID=28512, URL=127.0.0.1:9000, cql port=9042, redis port=6379
2017-09-06 22:53:40,890 INFO: Server is running: type=tserver, node_id=2, PID=28516, URL=127.0.0.1:9001, cql port=9043, redis port=6380
2017-09-06 22:53:40,894 INFO: Server is running: type=tserver, node_id=3, PID=28519, URL=127.0.0.1:9002, cql port=9044, redis port=6381
```

## Test CQL service

### Connect with cqlsh

[**cqlsh**](http://cassandra.apache.org/doc/latest/tools/cqlsh.html) is a command line shell for interacting with Apache Cassandra through [CQL (the Cassandra Query Language)](http://cassandra.apache.org/doc/latest/cql/index.html). It utilizes the Python CQL driver, and connects to the single node specified on the command line.

- Run cqlsh

For ease of use, the YugaByte DB package ships with the 3.10 version of cqlsh in its bin directory.

```sh
$ ./bin/cqlsh
Connected to local cluster at 127.0.0.1:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cqlsh> 
```

- Run a cql command

```sql
cqlsh> describe keyspaces;

system_schema  redis_keyspace  system_auth  system  default_keyspace

cqlsh> 
```

### Run a CQL sample app

- Verify that Java is installed on your localhost.

```sh
$ java -version
```

- Run the CQL time-series sample app using the executable jar

```sh
$ java -jar ./java/yb-sample-apps.jar --workload CassandraTimeseries --nodes localhost:9042
2017-09-19 04:07:13,675 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Using a randomly generated UUID : a22ba332-70bd-4c1d-b5ba-dbbf6e1298bb
2017-09-19 04:07:13,682 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] App: CassandraTimeseries
2017-09-19 04:07:13,682 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Adding node: localhost:9042
2017-09-19 04:07:13,683 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num reader threads: 1, num writer threads: 16
2017-09-19 04:07:13,688 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num unique keys to insert: 0
2017-09-19 04:07:13,688 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num keys to update: -1
2017-09-19 04:07:13,688 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num keys to read: -1
2017-09-19 04:07:13,688 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Value size: 100
2017-09-19 04:07:13,688 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Restrict values to ASCII strings: false
2017-09-19 04:07:13,688 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Perform sanity check at end of app run: false
2017-09-19 04:07:13,688 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Table TTL (secs): 86400
2017-09-19 04:07:13,688 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Local reads: false
2017-09-19 04:07:13,688 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Read only load: false
2017-09-19 04:07:14,973 [INFO|com.yugabyte.sample.apps.AppBase|AppBase] Dropped Cassandra table ts_metrics_raw using query: [DROP TABLE IF EXISTS ts_metrics_raw;]
2017-09-19 04:07:16,698 [INFO|com.yugabyte.sample.apps.AppBase|AppBase] Created a Cassandra table using query: [CREATE TABLE IF NOT EXISTS ts_metrics_raw (  user_id varchar, metric_id varchar, node_id varchar, ts timestamp, value varchar, primary key ((user_id, metric_id), node_id, ts)) WITH default_time_to_live = 86400;]
log4j:WARN No appenders could be found for logger (io.netty.util.ResourceLeakDetector).
log4j:WARN Please initialize the log4j system properly.
log4j:WARN See http://logging.apache.org/log4j/1.2/faq.html#noconfig for more info.
2017-09-19 04:07:18,895 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 0.59 ops/sec (225.00 ms/op), 3 total ops  |  Write: 162.78 ops/sec (290.38 ms/op), 822 total ops  |  Uptime: 5206 ms | Verification: ON | 
2017-09-19 04:07:23,898 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 25.98 ops/sec (39.42 ms/op), 133 total ops  |  Write: 1722.46 ops/sec (55.21 ms/op), 9439 total ops  |  Uptime: 10209 ms | Verification: ON | 
2017-09-19 04:07:28,902 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 38.97 ops/sec (25.72 ms/op), 328 total ops  |  Write: 1904.02 ops/sec (56.59 ms/op), 18967 total ops  |  Uptime: 15213 ms | Verification: ON | 
2017-09-19 04:07:33,903 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 47.99 ops/sec (20.90 ms/op), 568 total ops  |  Write: 2014.28 ops/sec (54.81 ms/op), 29040 total ops  |  Uptime: 20214 ms | Verification: ON | 
```

As you can see above, the CQL time-series sample app first creates a keyspace `ybdemo_keyspace` and a table `ts_metrics_raw`. It then starts multiple writer and reader threads to generate the load. The read/write ops count and latency metrics observed should not be used for performance testing purposes.

- Verify using cqlsh

```sql
cqlsh> use ybdemo_keyspace;
cqlsh:ybdemo_keyspace> describe tables;

ts_metrics_raw

cqlsh:ybdemo_keyspace> describe ts_metrics_raw

CREATE TABLE ybdemo_keyspace.ts_metrics_raw (
    user_id text,
    metric_id text,
    node_id text,
    ts timestamp,
    value text,
    PRIMARY KEY ((user_id, metric_id), node_id, ts)
) WITH CLUSTERING ORDER BY (node_id ASC, ts ASC);

cqlsh:ybdemo_keyspace> select * from ts_metrics_raw limit 10;

 user_id                                 | metric_id                 | node_id    | ts                              | value
-----------------------------------------+---------------------------+------------+---------------------------------+--------------------------
 48-b406bcaa-efd6-4cc4-b13d-2826098bef28 | metric-00007.yugabyte.com | node-00000 | 2017-06-29 16:37:12.000000+0000 | 1498754232000[B@3810f07d
 48-b406bcaa-efd6-4cc4-b13d-2826098bef28 | metric-00007.yugabyte.com | node-00000 | 2017-06-29 16:37:13.000000+0000 | 1498754233000[B@4507cba7
 48-b406bcaa-efd6-4cc4-b13d-2826098bef28 | metric-00007.yugabyte.com | node-00000 | 2017-06-29 16:37:14.000000+0000 | 1498754234000[B@23a7416b
 48-b406bcaa-efd6-4cc4-b13d-2826098bef28 | metric-00007.yugabyte.com | node-00000 | 2017-06-29 16:37:15.000000+0000 | 1498754235000[B@7eb9c5f4
 48-b406bcaa-efd6-4cc4-b13d-2826098bef28 | metric-00007.yugabyte.com | node-00000 | 2017-06-29 16:37:16.000000+0000 | 1498754236000[B@4957efc7
 48-b406bcaa-efd6-4cc4-b13d-2826098bef28 | metric-00007.yugabyte.com | node-00000 | 2017-06-29 16:37:17.000000+0000 | 1498754237000[B@2a4c4735
 48-b406bcaa-efd6-4cc4-b13d-2826098bef28 | metric-00007.yugabyte.com | node-00000 | 2017-06-29 16:37:18.000000+0000 | 1498754238000[B@63638401
 48-b406bcaa-efd6-4cc4-b13d-2826098bef28 | metric-00007.yugabyte.com | node-00000 | 2017-06-29 16:37:19.000000+0000 | 1498754239000[B@419a913d
 48-b406bcaa-efd6-4cc4-b13d-2826098bef28 | metric-00007.yugabyte.com | node-00000 | 2017-06-29 16:37:20.000000+0000 | 1498754240000[B@25b246f2
 48-b406bcaa-efd6-4cc4-b13d-2826098bef28 | metric-00007.yugabyte.com | node-00000 | 2017-06-29 16:37:21.000000+0000 | 1498754241000[B@38d35bcc

(10 rows)
cqlsh:ybdemo_keyspace>

```

## Test Redis service 

### Setup Redis service

Setup the `redis_keyspace` keyspace and the `.redis` table so that this cluster becomes ready for redis clients. Detailed output for the *setup_redis* command is available in the [yb-ctl Reference](/admin/yb-ctl/#setup-redis)

```sh
$ ./bin/yb-ctl setup_redis
```

### Connect with redis-cli

[redis-cli](https://redis.io/topics/rediscli) is a command line interface to interact with a Redis server. 

- Run redis-cli

For ease of use, the YugaByte DB package ships with the 4.0.1 version of redis-cli in its bin directory.

```
$ ./bin/redis-cli
127.0.0.1:6379> set mykey somevalue
OK
127.0.0.1:6379> get mykey
"somevalue"
```

### Run a Redis sample app

- Verify that Java is installed on your localhost.

```sh
$ java -version
```

- Run the Redis key-value sample app using the executable jar

```sh
$ java -jar ./java/yb-sample-apps.jar --workload RedisKeyValue --nodes localhost:6379 --nouuid
2017-09-06 23:12:41,400 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Using a randomly generated UUID : 6b39fd6f-e1ba-4a42-886f-120a4990043f
2017-09-06 23:12:41,411 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] App: RedisKeyValue
2017-09-06 23:12:41,411 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Adding node: localhost:6379
2017-09-06 23:12:41,415 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num reader threads: 32, num writer threads: 2
2017-09-06 23:12:41,421 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num unique keys to insert: 1000000
2017-09-06 23:12:41,421 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num keys to update: -1000001
2017-09-06 23:12:41,421 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num keys to read: -1
2017-09-06 23:12:41,421 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Value size: 0
2017-09-06 23:12:41,421 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Restrict values to ASCII strings: false
2017-09-06 23:12:41,421 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Perform sanity check at end of app run: false
2017-09-06 23:12:41,421 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Table TTL (secs): -1
2017-09-06 23:12:41,421 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Local reads: false
2017-09-06 23:12:41,421 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Read only load: false
2017-09-06 23:12:46,800 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 0.00 ops/sec (0.00 ms/op), 0 total ops  |  Write: 0.00 ops/sec (0.00 ms/op), 0 total ops  |  Uptime: 5378 ms | 
2017-09-06 23:12:51,803 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 6448.45 ops/sec (3.46 ms/op), 34598 total ops  |  Write: 2.37 ops/sec (1720.17 ms/op), 12 total ops  |  Uptime: 10381 ms | 
2017-09-06 23:12:56,817 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 4898.52 ops/sec (6.55 ms/op), 59137 total ops  |  Write: 248.06 ops/sec (8.14 ms/op), 1256 total ops  |  Uptime: 15395 ms | 
2017-09-06 23:13:01,902 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 4781.90 ops/sec (6.69 ms/op), 83479 total ops  |  Write: 267.66 ops/sec (7.48 ms/op), 2617 total ops  |  Uptime: 20480 ms | 
2017-09-06 23:13:06,906 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 4569.34 ops/sec (6.98 ms/op), 106345 total ops  |  Write: 257.58 ops/sec (7.74 ms/op), 3906 total ops  |  Uptime: 25484 ms | 
2017-09-06 23:13:11,912 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 4589.08 ops/sec (6.99 ms/op), 129297 total ops  |  Write: 259.31 ops/sec (7.73 ms/op), 5204 total ops  |  Uptime: 30490 ms | 

```

- Verify with redis-cli

```sh
$ ./bin/redis-cli
127.0.0.1:6379> get key:1  
"val:1"  
127.0.0.1:6379> get key:2  
"val:2"  
127.0.0.1:6379> get key:100  
"val:100"  
127.0.0.1:6379>   
```
