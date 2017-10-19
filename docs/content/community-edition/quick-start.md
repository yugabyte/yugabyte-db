---
date: 2016-03-09T00:11:02+01:00
title: Quick Start
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

Download the YugaByte CE package [here](http://www.yugabyte.com#download). Thereafter, follow the instructions below.

### Install

On your localhost, execute the following commands.

```sh
$ mkdir ~/yugabyte
$ tar xvfz yugabyte.ce.<version>.tar.gz -C yugabyte
$ cd yugabyte
```

Run the **configure** script to ensure all dependencies get auto-installed. This script will also install a couple of libraries (`cyrus-sasl`, `cyrus-sasl-plain` and `file`) and will request for a sudo password in case you are not running the script as root.

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

Run the command below to see that we now have 3 `yb-master` processes and 3 `yb-tserver` processes running on this localhost. Roles played by these processes in a YugaByte cluster (aka Universe) is explained in detail [here](/architecture/concepts/#universe-components).

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
$ java -jar ./java/yb-sample-apps.jar --workload CassandraTimeseries --nodes 127.0.0.1:9042,127.0.0.1:9043,127.0.0.1:9044
2017-09-25 21:52:31,161 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Using a randomly generated UUID : 8718b716-6ddd-4d2a-9552-adae9564b75c
2017-09-25 21:52:31,181 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] App: CassandraTimeseries
2017-09-25 21:52:31,181 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Adding node: 127.0.0.1:9042
2017-09-25 21:52:31,182 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Adding node: 127.0.0.1:9043
2017-09-25 21:52:31,188 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Adding node: 127.0.0.1:9044
2017-09-25 21:52:31,188 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num reader threads: 1, num writer threads: 16
2017-09-25 21:52:31,188 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num unique keys to insert: 0
2017-09-25 21:52:31,188 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num keys to update: -1
2017-09-25 21:52:31,188 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num keys to read: -1
2017-09-25 21:52:31,188 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Value size: 100
2017-09-25 21:52:31,188 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Restrict values to ASCII strings: false
2017-09-25 21:52:31,188 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Perform sanity check at end of app run: false
2017-09-25 21:52:31,189 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Table TTL (secs): 86400
2017-09-25 21:52:31,189 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Local reads: false
2017-09-25 21:52:31,189 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Read only load: false
2017-09-25 21:52:32,589 [INFO|com.yugabyte.sample.apps.AppBase|AppBase] Dropped Cassandra table ts_metrics_raw using query: [DROP TABLE IF EXISTS ts_metrics_raw;]
2017-09-25 21:52:34,052 [INFO|com.yugabyte.sample.apps.AppBase|AppBase] Created a Cassandra table using query: [CREATE TABLE IF NOT EXISTS ts_metrics_raw (  user_id varchar, metric_id varchar, node_id varchar, ts timestamp, value varchar, primary key ((user_id, metric_id), node_id, ts)) WITH default_time_to_live = 86400;]
log4j:WARN No appenders could be found for logger (io.netty.util.ResourceLeakDetector).
log4j:WARN Please initialize the log4j system properly.
log4j:WARN See http://logging.apache.org/log4j/1.2/faq.html#noconfig for more info.
2017-09-25 21:52:36,382 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 0.00 ops/sec (0.00 ms/op), 0 total ops  |  Write: 291.74 ops/sec (111.89 ms/op), 1467 total ops  |  Uptime: 5193 ms | Verification: ON | 
2017-09-25 21:52:41,385 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 13.57 ops/sec (97.72 ms/op), 68 total ops  |  Write: 1332.13 ops/sec (85.45 ms/op), 8131 total ops  |  Uptime: 10196 ms | Verification: ON | 
2017-09-25 21:52:46,386 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 31.39 ops/sec (31.83 ms/op), 225 total ops  |  Write: 1825.45 ops/sec (58.61 ms/op), 17260 total ops  |  Uptime: 15197 ms | Verification: ON | 
2017-09-25 21:52:51,390 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 43.97 ops/sec (22.80 ms/op), 445 total ops  |  Write: 1845.58 ops/sec (61.03 ms/op), 26495 total ops  |  Uptime: 20201 ms | Verification: ON | 
2017-09-25 21:52:56,395 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 69.53 ops/sec (14.39 ms/op), 793 total ops  |  Write: 1983.04 ops/sec (56.99 ms/op), 36420 total ops  |  Uptime: 25206 ms | Verification: ON | 
2017-09-25 21:53:01,405 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 62.96 ops/sec (15.86 ms/op), 1108 total ops  |  Write: 1947.12 ops/sec (57.78 ms/op), 46176 total ops  |  Uptime: 30216 ms | Verification: ON | 
```

As you can see above, the CQL time-series sample app first creates a keyspace `ybdemo_keyspace` and a table `ts_metrics_raw`. It then starts multiple writer and reader threads to generate the load. The read/write ops count and latency metrics observed should not be used for performance testing purposes.

- Verify using cqlsh

```sql
$ ./bin/cqlsh
Connected to local cluster at 127.0.0.1:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
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
2017-09-26 00:24:58,645 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Using NO UUID
2017-09-26 00:24:58,662 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] App: RedisKeyValue
2017-09-26 00:24:58,662 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Adding node: localhost:6379
2017-09-26 00:24:58,664 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num reader threads: 32, num writer threads: 2
2017-09-26 00:24:58,671 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num unique keys to insert: 1000000
2017-09-26 00:24:58,671 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num keys to update: -1000001
2017-09-26 00:24:58,671 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num keys to read: -1
2017-09-26 00:24:58,671 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Value size: 0
2017-09-26 00:24:58,671 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Restrict values to ASCII strings: false
2017-09-26 00:24:58,671 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Perform sanity check at end of app run: false
2017-09-26 00:24:58,671 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Table TTL (secs): -1
2017-09-26 00:24:58,671 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Local reads: false
2017-09-26 00:24:58,671 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Read only load: false
2017-09-26 00:25:03,884 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 697.86 ops/sec (7.20 ms/op), 3492 total ops  |  Write: 70.45 ops/sec (28.34 ms/op), 363 total ops  |  Uptime: 5211 ms | 
2017-09-26 00:25:08,886 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 5104.97 ops/sec (6.09 ms/op), 30075 total ops  |  Write: 300.28 ops/sec (6.66 ms/op), 1882 total ops  |  Uptime: 10213 ms | 
2017-09-26 00:25:13,891 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 5479.23 ops/sec (5.84 ms/op), 57497 total ops  |  Write: 309.30 ops/sec (6.46 ms/op), 3430 total ops  |  Uptime: 15218 ms | 
2017-09-26 00:25:18,897 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 5490.49 ops/sec (5.83 ms/op), 84985 total ops  |  Write: 295.02 ops/sec (6.78 ms/op), 4907 total ops  |  Uptime: 20224 ms | 
2017-09-26 00:25:23,898 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 5318.47 ops/sec (6.02 ms/op), 111583 total ops  |  Write: 284.14 ops/sec (7.04 ms/op), 6328 total ops  |  Uptime: 25225 ms | 
2017-09-26 00:25:28,899 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 5103.60 ops/sec (6.27 ms/op), 137105 total ops  |  Write: 274.16 ops/sec (7.29 ms/op), 7699 total ops  |  Uptime: 30226 ms | 
2017-09-26 00:25:33,904 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 5586.69 ops/sec (5.73 ms/op), 165065 total ops  |  Write: 297.92 ops/sec (6.72 ms/op), 9190 total ops  |  Uptime: 35231 ms | 
2017-09-26 00:25:38,908 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 5419.40 ops/sec (5.90 ms/op), 192184 total ops  |  Write: 295.16 ops/sec (6.78 ms/op), 10667 total ops  |  Uptime: 40235 ms | 
2017-09-26 00:25:43,909 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 5288.26 ops/sec (6.05 ms/op), 218632 total ops  |  Write: 286.12 ops/sec (6.98 ms/op), 12098 total ops  |  Uptime: 45236 ms |
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



## Test fault tolerance

- Start a new local cluster on terminal 1.

```sh
# destroy any previously running local cluster
$ ./bin/yb-ctl destroy

# create a new local cluster
$ ./bin/yb-ctl create
```

- Start the CQL sample app on terminal 2. Observe the output with 3 `yb-tserver` nodes.

```sh
$ java -jar ./java/yb-sample-apps.jar --workload CassandraTimeseries --nodes 127.0.0.1:9042,127.0.0.1:9043,127.0.0.1:9044
```

- Remove one node to bring the cluster to only 2 `yb-tserver` nodes.

```sh
# check status and get node_ids
$ ./bin/yb-ctl status
2017-09-25 22:07:07,655 INFO: Server is running: type=master, node_id=1, PID=11140, URL=127.0.0.1:7000
2017-09-25 22:07:08,596 INFO: Server is running: type=master, node_id=2, PID=11143, URL=127.0.0.1:7001
2017-09-25 22:07:11,450 INFO: Server is running: type=master, node_id=3, PID=11146, URL=127.0.0.1:7002
2017-09-25 22:07:12,328 INFO: Server is running: type=tserver, node_id=1, PID=11149, URL=127.0.0.1:9000, cql port=9042, redis port=6379
2017-09-25 22:07:12,811 INFO: Server is running: type=tserver, node_id=2, PID=11152, URL=127.0.0.1:9001, cql port=9043, redis port=6380
2017-09-25 22:07:13,120 INFO: Server is running: type=tserver, node_id=3, PID=11155, URL=127.0.0.1:9002, cql port=9044, redis port=6381

# remove node 1 from local cluster
$ ./bin/yb-ctl remove_node 1
2017-09-25 22:07:28,570 INFO: Removing server type=tserver node_id=1
2017-09-25 22:07:29,198 INFO: Stopping server type=tserver node_id=1 PID=11149
2017-09-25 22:07:29,212 INFO: Waiting for server type=tserver node_id=1 PID=11149 to stop...
2017-09-25 22:07:30,007 INFO: Waiting for server type=tserver node_id=1 PID=11149 to stop...
2017-09-25 22:07:30,653 INFO: Waiting for server type=tserver node_id=1 PID=11149 to stop...

# check status again
$ ./bin/yb-ctl status
2017-09-25 22:07:40,387 INFO: Server is running: type=master, node_id=1, PID=11140, URL=127.0.0.1:7000
2017-09-25 22:07:40,905 INFO: Server is running: type=master, node_id=2, PID=11143, URL=127.0.0.1:7001
2017-09-25 22:07:41,398 INFO: Server is running: type=master, node_id=3, PID=11146, URL=127.0.0.1:7002
2017-09-25 22:07:41,823 INFO: Server type=tserver node_id=1 is not running
2017-09-25 22:07:41,856 INFO: Server is running: type=tserver, node_id=2, PID=11152, URL=127.0.0.1:9001, cql port=9043, redis port=6380
2017-09-25 22:07:41,862 INFO: Server is running: type=tserver, node_id=3, PID=11155, URL=127.0.0.1:9002, cql port=9044, redis port=6381
```

- Observe sample app output again to see a temporary connectivity issue to the node that was just killed. The client immediately fails over to a live node and then overall throughput/latency remains unchanged. This is because even with 2 live nodes, YugaByte can continue to update majority of replicas (i.e. 2 out of 3 replicas) and hence can continue to process the writes from the CQL sample app.

- If you remove one more node then you can see that write operations from the CQL sample app no longer succeed. This is expected since the number of alive nodes (i.e. 1) is less than the number required to establish majority (i.e. 2 nodes out of 3).


## Test automatic rebalancing

- Start a new local cluster on terminal 1.

```sh
# destroy any previously running local cluster
$ ./bin/yb-ctl destroy

# create a new local cluster
$ ./bin/yb-ctl create
```

- Start the CQL sample app on terminal 2. Observe the output with 3 `yb-tserver` nodes.

```sh
$ java -jar ./java/yb-sample-apps.jar --workload CassandraTimeseries --nodes 127.0.0.1:9042,127.0.0.1:9043,127.0.0.1:9044
```

- Add a new node to the cluster. This will start a 4th `yb-tserver` process and give it  `node_id` 4 for tracking purposes.

```sh
# add a node
$ ./bin/yb-ctl add_node
2017-09-25 22:11:45,429 INFO: Starting tserver with:
/home/vagrant/yugabyte/bin/yb-tserver 
--fs_data_dirs "/tmp/yugabyte-local-cluster/disk1/node-4,/tmp/yugabyte-local-cluster/disk2/node-4" 
--webserver_port 9003 
--rpc_bind_addresses 127.0.0.1:9103 
--use_hybrid_clock=False 
--placement_cloud cloud 
--placement_region region 
--placement_zone zone 
--webserver_doc_root "/home/vagrant/yugabyte/www" 
--tserver_master_addrs 127.0.0.1:7100,127.0.0.1:7101,127.0.0.1:7102 
--memory_limit_hard_bytes 1073741824 
--redis_proxy_webserver_port 11003 
--redis_proxy_bind_address 127.0.0.1:6382 
--cql_proxy_webserver_port 12003 
--cql_proxy_bind_address 127.0.0.1:9045 
--local_ip_for_outbound_sockets 127.0.0.1 
>"/tmp/yugabyte-local-cluster/disk1/node-4/tserver.out" 2>"/tmp/yugabyte-local-cluster/disk1/node-4/tserver.err" &

# check status
$ ./bin/yb-ctl status
2017-09-25 22:16:37,858 INFO: Server is running: type=master, node_id=1, PID=11140, URL=127.0.0.1:7000
2017-09-25 22:16:37,865 INFO: Server is running: type=master, node_id=2, PID=11143, URL=127.0.0.1:7001
2017-09-25 22:16:37,872 INFO: Server is running: type=master, node_id=3, PID=11146, URL=127.0.0.1:7002
2017-09-25 22:16:37,878 INFO: Server is running: type=tserver, node_id=1, PID=11149, URL=127.0.0.1:9000, cql port=9042, redis port=6379
2017-09-25 22:16:37,885 INFO: Server is running: type=tserver, node_id=2, PID=11152, URL=127.0.0.1:9001, cql port=9043, redis port=6380
2017-09-25 22:16:37,891 INFO: Server is running: type=tserver, node_id=3, PID=11155, URL=127.0.0.1:9002, cql port=9044, redis port=6381
2017-09-25 22:16:37,898 INFO: Server is running: type=tserver, node_id=4, PID=13000, URL=127.0.0.1:9003, cql port=9045, redis port=6382
```

- Observe the CQL sample output again to see that the read and write throughput has increased to a higher steady state number. This is because the cluster automatically balanced some of its tablet-peer leaders into the newly added 4th node and thereafter let the client know to also use the 4th node for serving queries. This automatic balancing of the data as well as client queries is completely transparent to the application logic, allowing the application to scale linearly for both reads and writes. 
