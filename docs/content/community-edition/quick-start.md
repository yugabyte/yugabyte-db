---
date: 2016-03-09T00:11:02+01:00
title: Community Edition - Quick Start
weight: 15
---

 The easiest way to get started with the YugaByte Community Edition is to create a multi-node YugaByte **local cluster** on your laptop or desktop.

{{< note title="Note" >}}
Running local clusters is not recommended for production environments. You can either deploy the Community Edition manually on a set of instances or use the Enterprise Edition that automates all day-to-day operations including cluster administration across all major public clouds as well as on-premises datacenters.
{{< /note >}}

## Prerequisites

Operation systems supported for local clusters are

- Centos 7 or higher

## Download and install

Download the YugaByte DB package using the link you were provided at registration. Thereafter, follow the instructions below.

```sh
$ mkdir ~/yugabyte
$ tar xvfz yugabyte-community-<version>-centos.tar.gz -C yugabyte
$ cd yugabyte
```

Run the **configure** script to ensure all dependencies get auto-installed. This script will also install a two libraries (`cyrus-sasl` and `cyrus-sasl-plain`) and will request for a sudo password in case you are not running the script as root.

```sh
$ ./bin/configure
```

## Create local cluster

Create a 3 node cluster with replication factor 3 using the [yugabyte-cli](/community-edition/cli-reference) CLI that has a set of pre-built commands to manage a local cluster. 

```sh
$ ./bin/yugabyte-cli create
```

Detailed output for the *create* command is available in the [CQL Reference](/community-edition/cli-reference/#create-cluster)

Check the status of the cluster

```sh
$ ./bin/yugabyte-cli status
2017-09-06 22:53:40,871 INFO: Server is running: type=master, node_id=1, PID=28494, URL=127.0.0.1:7000
2017-09-06 22:53:40,876 INFO: Server is running: type=master, node_id=2, PID=28504, URL=127.0.0.1:7001
2017-09-06 22:53:40,881 INFO: Server is running: type=master, node_id=3, PID=28507, URL=127.0.0.1:7002
2017-09-06 22:53:40,885 INFO: Server is running: type=tserver, node_id=1, PID=28512, URL=127.0.0.1:9000, cql port=9042, redis port=6379
2017-09-06 22:53:40,890 INFO: Server is running: type=tserver, node_id=2, PID=28516, URL=127.0.0.1:9001, cql port=9043, redis port=6380
2017-09-06 22:53:40,894 INFO: Server is running: type=tserver, node_id=3, PID=28519, URL=127.0.0.1:9002, cql port=9044, redis port=6381
```

Setup the `redis_keyspace` keyspace and the `.redis` table so that this cluster becomes ready for redis clients.

```sh
$ ./bin/yugabyte-cli setup_redis
```

Detailed output for the *setup_redis* command is available in the [CQL Reference](/community-edition/cli-reference/#setup-redis)


## Connect with cqlsh or redis-cli

### cqlsh

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

### redis-cli

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

## Run a sample app

- Verify that Java is installed on your localhost.

```sh
$ java -version
```

### Cassandra sample app

- Run the Cassandra time-series sample app using the executable jar

```sh
$ java -jar ./java/yb-sample-apps.jar --workload CassandraTimeseries --nodes localhost:9042
2017-09-06 23:14:48,817 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Using a randomly generated UUID : b03bb542-7009-4302-8141-f30c2245181b
2017-09-06 23:14:48,835 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] App: CassandraTimeseries
2017-09-06 23:14:48,836 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Adding node: localhost:9042
2017-09-06 23:14:48,838 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num reader threads: 1, num writer threads: 16
2017-09-06 23:14:48,843 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num unique keys to insert: 0
2017-09-06 23:14:48,843 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num keys to update: -1
2017-09-06 23:14:48,843 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num keys to read: -1
2017-09-06 23:14:48,843 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Value size: 100
2017-09-06 23:14:48,843 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Restrict values to ASCII strings: false
2017-09-06 23:14:48,843 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Perform sanity check at end of app run: false
2017-09-06 23:14:48,843 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Table TTL (secs): 86400
2017-09-06 23:14:48,843 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Local reads: false
2017-09-06 23:14:48,844 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Read only load: false
2017-09-06 23:14:50,215 [INFO|com.yugabyte.sample.apps.AppBase|AppBase] Ignoring exception dropping table: SQL error (yb/sql/ptree/process_context.cc:41): Table Not Found - Not found (yb/common/wire_protocol.cc:122): The table does not exist: table_name: "ts_metrics_raw"
namespace {
  name: "ybdemo_keyspace"
}

DROP TABLE ts_metrics_raw;
           ^^^^^^^^^^^^^^
 (error -301)
2017-09-06 23:14:54,046 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 0.00 ops/sec (0.00 ms/op), 0 total ops  |  Write: 0.00 ops/sec (0.00 ms/op), 0 total ops  |  Uptime: 5202 ms | Verification: ON | 
2017-09-06 23:14:54,337 [INFO|com.yugabyte.sample.apps.AppBase|AppBase] Created a Cassandra table using query: [CREATE TABLE IF NOT EXISTS ts_metrics_raw (  user_id varchar, metric_id varchar, node_id varchar, ts timestamp, value varchar, primary key ((user_id, metric_id), node_id, ts)) WITH default_time_to_live = 86400;]
log4j:WARN No appenders could be found for logger (io.netty.util.ResourceLeakDetector).
log4j:WARN Please initialize the log4j system properly.
log4j:WARN See http://logging.apache.org/log4j/1.2/faq.html#noconfig for more info.
2017-09-06 23:14:59,047 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 12.78 ops/sec (51.84 ms/op), 64 total ops  |  Write: 700.03 ops/sec (145.36 ms/op), 3501 total ops  |  Uptime: 10203 ms | Verification: ON | 
2017-09-06 23:15:04,052 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 59.74 ops/sec (16.75 ms/op), 363 total ops  |  Write: 1085.97 ops/sec (105.62 ms/op), 8936 total ops  |  Uptime: 15208 ms | Verification: ON | 
2017-09-06 23:15:09,054 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 98.16 ops/sec (10.17 ms/op), 854 total ops  |  Write: 1172.08 ops/sec (98.10 ms/op), 14799 total ops  |  Uptime: 20210 ms | Verification: ON | 
2017-09-06 23:15:14,058 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 93.34 ops/sec (10.72 ms/op), 1321 total ops  |  Write: 1129.43 ops/sec (102.95 ms/op), 20450 total ops  |  Uptime: 25214 ms | Verification: ON | 
2017-09-06 23:15:19,058 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 92.18 ops/sec (10.85 ms/op), 1782 total ops  |  Write: 1002.22 ops/sec (117.08 ms/op), 25462 total ops  |  Uptime: 30214 ms | Verification: ON | 
2017-09-06 23:15:24,060 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 94.37 ops/sec (10.58 ms/op), 2254 total ops  |  Write: 1100.50 ops/sec (106.65 ms/op), 30966 total ops  |  Uptime: 35216 ms | Verification: ON | 
2017-09-06 23:15:29,060 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 99.59 ops/sec (10.05 ms/op), 2752 total ops  |  Write: 1127.67 ops/sec (103.92 ms/op), 36605 total ops  |  Uptime: 40216 ms | Verification: ON | 
2017-09-06 23:15:34,061 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 101.19 ops/sec (9.88 ms/op), 3258 total ops  |  Write: 1106.70 ops/sec (105.74 ms/op), 42139 total ops  |  Uptime: 45217 ms | Verification: ON | 
2017-09-06 23:15:39,065 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 101.72 ops/sec (9.83 ms/op), 3767 total ops  |  Write: 1046.02 ops/sec (110.82 ms/op), 47373 total ops  |  Uptime: 50221 ms | Verification: ON |
```

As you can see above, the Cassandra time-series sample app first creates a keyspace `ybdemo_keyspace` and a table `ts_metrics_raw`. It then starts multiple writer and reader threads to generate the load. The read/write ops count and latency metrics observed should not be used for performance testing purposes.

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

### Redis sample app

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
```

## Test automatic rebalancing


Add a new node to the cluster. This will start a new yb-tserver process and give it a new `node_id` for tracking purposes.

```sh
$ ./bin/yugabyte-cli add_node
2017-08-24 20:32:18,015 INFO: Starting tserver with:
/home/vagrant/yugabyte/bin/yb-tserver 
--fs_data_dirs "/tmp/yugabyte-local-cluster/tserver-4/data1,/tmp/yugabyte-local-cluster/tserver-4/data2" 
--fs_wal_dirs "/tmp/yugabyte-local-cluster/tserver-4/wal1,/tmp/yugabyte-local-cluster/tserver-4/wal2" 
--log_dir "/tmp/yugabyte-local-cluster/tserver-4/logs" 
--use_hybrid_clock=false 
--webserver_port 9004 
--rpc_bind_addresses 127.0.0.1:9104 
--placement_cloud cloud 
--placement_region region 
--placement_zone zone 
--webserver_doc_root "/home/vagrant/yugabyte/www" 
--tserver_master_addrs 127.0.0.1:7101,127.0.0.1:7102,127.0.0.1:7103 
--memory_limit_hard_bytes 268435456 
--redis_proxy_webserver_port 11004 
--redis_proxy_bind_address 127.0.0.1:10104 
--cql_proxy_webserver_port 12004 
--cql_proxy_bind_address 127.0.0.1:9046 
--local_ip_for_outbound_sockets 127.0.0.1 
--memstore_size_mb 2 
>"/tmp/yugabyte-local-cluster/tserver-4/tserver.out" 2>"/tmp/yugabyte-local-cluster/tserver-4/tserver.err" &
```

## Test fault tolerance

Remove one node to bring the cluster back to 3 nodes.

```sh
$ $ ./bin/yugabyte-cli remove_node 4
2017-08-24 20:28:30,919 INFO: Removing server type=tserver index=2
2017-08-24 20:28:30,925 INFO: Stopping server type=tserver index=2 PID=6298
2017-08-24 20:28:30,925 INFO: Waiting for server type=tserver index=2 PID=6298 to stop...
```

Remove another node to see that the cluster continues to process client requests even with the 2 remaining nodes.

```sh
$ $ ./bin/yugabyte-cli remove_node 3
2017-08-24 20:28:30,919 INFO: Removing server type=tserver index=2
2017-08-24 20:28:30,925 INFO: Stopping server type=tserver index=2 PID=6298
2017-08-24 20:28:30,925 INFO: Waiting for server type=tserver index=2 PID=6298 to stop...
```


