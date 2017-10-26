## Test CQL service

[**cqlsh**](http://cassandra.apache.org/doc/latest/tools/cqlsh.html) is a command line shell for interacting with Apache Cassandra through [CQL (the Cassandra Query Language)](http://cassandra.apache.org/doc/latest/cql/index.html). It utilizes the Python CQL driver, and connects to the single node specified on the command line. For ease of use, the YugaByte DB linux package ships with the 3.10 version of cqlsh in its bin directory.

### Connect with cqlsh

- Run cqlsh

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

system_schema  system_auth  system  default_keyspace

cqlsh> 
```

### Run a CQL sample app

- Verify that Java is installed on your localhost.

```sh
$ java -version
```

- Run the CQL time-series sample app using the executable jar

```sh
$ java -jar ./java/yb-sample-apps.jar --workload CassandraTimeseries --nodes 127.0.0.1:9042,127.0.0.2:9042,127.0.0.3:9042
2017-09-25 21:52:31,161 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Using a randomly generated UUID : 8718b716-6ddd-4d2a-9552-adae9564b75c
2017-09-25 21:52:31,181 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] App: CassandraTimeseries
2017-09-25 21:52:31,181 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Adding node: 127.0.0.1:9042
2017-09-25 21:52:31,182 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Adding node: 127.0.0.2:9042
2017-09-25 21:52:31,188 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Adding node: 127.0.0.3:9042
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

cqlsh:ybdemo_keyspace> describe ybdemo_keyspace.ts_metrics_raw;

CREATE TABLE ybdemo_keyspace.ts_metrics_raw (
    user_id text,
    metric_id text,
    node_id text,
    ts timestamp,
    value text,
    PRIMARY KEY ((user_id, metric_id), node_id, ts)
) WITH CLUSTERING ORDER BY (node_id ASC, ts ASC)
    AND default_time_to_live = 86400;

cqlsh:ybdemo_keyspace> select * from ybdemo_keyspace.ts_metrics_raw limit 10;

 user_id                                 | metric_id                 | node_id    | ts                              | value
-----------------------------------------+---------------------------+------------+---------------------------------+--------------------------
 16-d3716870-4f69-413f-bb21-9187d9008c8b | metric-00008.yugabyte.com | node-00000 | 2017-10-16 22:27:38.000000+0000 | 1508192858000[B@486a3f81
 16-d3716870-4f69-413f-bb21-9187d9008c8b | metric-00008.yugabyte.com | node-00000 | 2017-10-16 22:27:39.000000+0000 |  1508192859000[B@ce264eb
 16-d3716870-4f69-413f-bb21-9187d9008c8b | metric-00008.yugabyte.com | node-00000 | 2017-10-16 22:27:40.000000+0000 | 1508192860000[B@13037655
 16-d3716870-4f69-413f-bb21-9187d9008c8b | metric-00008.yugabyte.com | node-00000 | 2017-10-16 22:27:41.000000+0000 | 1508192861000[B@53078d21
 16-d3716870-4f69-413f-bb21-9187d9008c8b | metric-00008.yugabyte.com | node-00000 | 2017-10-16 22:27:42.000000+0000 | 1508192862000[B@2361e598
 16-d3716870-4f69-413f-bb21-9187d9008c8b | metric-00008.yugabyte.com | node-00000 | 2017-10-16 22:27:43.000000+0000 | 1508192863000[B@6af39865
 16-d3716870-4f69-413f-bb21-9187d9008c8b | metric-00008.yugabyte.com | node-00000 | 2017-10-16 22:27:44.000000+0000 | 1508192864000[B@7533ebbe
 16-d3716870-4f69-413f-bb21-9187d9008c8b | metric-00008.yugabyte.com | node-00005 | 2017-10-16 22:27:45.000000+0000 | 1508192865000[B@48ad18ee
 16-d3716870-4f69-413f-bb21-9187d9008c8b | metric-00008.yugabyte.com | node-00005 | 2017-10-16 22:27:46.000000+0000 | 1508192866000[B@17780377
 16-d3716870-4f69-413f-bb21-9187d9008c8b | metric-00008.yugabyte.com | node-00005 | 2017-10-16 22:27:47.000000+0000 | 1508192867000[B@64bd6f43

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

For ease of use, the YugaByte DB linux package ships with the 4.0.1 version of redis-cli in its bin directory.

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
