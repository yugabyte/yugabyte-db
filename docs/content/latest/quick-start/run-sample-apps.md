---
title: 5. Run Sample Apps
linkTitle: 5. Run Sample Apps
description: Run Sample Apps
aliases:
  - /quick-start/run-sample-apps/
menu:
  latest:
    parent: quick-start
    weight: 150
---

After trying out [Cassandra](../test-cassandra/) and [Redis](../test-redis/) cli based commands on the local cluster, follow the instructions below to run some sample apps against the cluster as well as look at some of the configuration details of the cluster.

We will show how to run a sample key-value workload against both the CQL service and the Redis service.

## Prerequisites

- Verify that Java is installed on your localhost. You need Java 1.8 installed on your system in order to run sample applications. You can install Java 1.8 from [here](http://www.oracle.com/technetwork/java/javase/downloads/jdk8-downloads-2133151.html).

```{.sh .copy .separator-dollar}
$ java -version
```

- Make sure you have `yb-sample-apps.jar` in a convenient location.

The YugaByte DB install comes pre-packaged with the sample apps and the corresponding source code bundled as a self-contained JAR. 

For binary installs, this is located in the `java` subdirectory of the install. 

For docker based installs, you can copy it from one of the YB containers into the `yugabyte` install directory.

```{.sh .copy .separator-dollar}
$ docker cp yb-master-n1:/home/yugabyte/java/yb-sample-apps.jar .
```

For Kubernetes based installs, you can copy it from one of the YB pods into the `yugabyte` install directory.

```{.sh .copy .separator-dollar}
$ kubectl cp yb-master-0:/home/yugabyte/java/yb-sample-apps.jar .
```


## Key-Value Workload using Cassandra API

### 1. Run the app

You can run the CQL `CassandraKeyValue` sample app using the following command.

```{.sh .copy .separator-dollar}
$ java -jar yb-sample-apps.jar --workload CassandraKeyValue --nodes 127.0.0.1:9042
```

It first creates a keyspace `ybdemo_keyspace` and a table `CassandraKeyValue`, then starts multiple writer and reader threads to generate the load. The read/write ops count and latency metrics observed should not be used for performance testing purposes. Below is a short description of the output logs, which will help in understanding what the workload is doing.

### 2. Configure number of reader and writer threads

The number of reader and writer threads can be tuned in the sample app using the `--num_threads_read <num>` and the `--num_threads_write <num>` flags.

```sh
2017-10-28 13:41:43,948 [INFO|...|CmdLineOpts] Num reader threads: 24, num writer threads: 2
```

This log line prints out how many threads are performing reads and writes. By default, 24 threads perform 1 outstanding read each and 2 threads perform 1 outstanding write each.


### 3. Configure Time to Live (TTL)

You can specify the time after which you want the data to get automatically expired and purged out from the database usint the `--table_ttl_seconds <num seconds>` option. Setting this to -1 implies no TTL and the data to never expires.

```sh
2017-10-28 13:41:43,952 [INFO|...|CmdLineOpts] Table TTL (secs): -1
```

By default, the sample app creates a table with no TTL.

### 4. Create a key-value table

The sample app creates a table which has two columns - a key column of type `VARCHAR` and a value column of type `BLOB`.

```sh
Dropped Cassandra table CassandraKeyValue using query:
  [DROP TABLE IF EXISTS CassandraKeyValue;]
Created a Cassandra table using query:
  [CREATE TABLE IF NOT EXISTS CassandraKeyValue (k varchar, v blob, primary key (k));]
```

### 5. Review runtime read and write stats

The sample app prints out its read and write stats every 5 seconds. The log lines would look like the following.

```sh
Read: 7104.28 ops/sec (3.37 ms/op), 65435 total ops  |  Write: 442.01 ops/sec (4.52 ms/op), 4087 total ops  |
      Uptime: 10011 ms | maxWrittenKey: 4081 | maxGeneratedKey: 4088
Read: 7080.17 ops/sec (3.39 ms/op), 100854 total ops  |  Write: 427.20 ops/sec (4.68 ms/op), 6224 total ops  |
       Uptime: 15013 ms | maxWrittenKey: 6220 | maxGeneratedKey: 6225
```


The first portion of each log line (shown below) says that the sample app is currently doing around 7K read ops/sec, with an average 3.37ms latency per op. It has done around 65K read ops so far using 24 (or the specified number) of reader threads.

```sh
Read: 7104.28 ops/sec (3.37 ms/op), 65435 total ops
```

The second portion of the log line (again shown below) says that the sample app is currently doing 427 writes/sec, each taking 4.68ms. It has done 6K ops total thus far, using the 2 (or speficied number of) writer threads. Note that if you are running the default configuration, each write has a replication factor of 3.

The last portion of each log line gives an idea of how long the sample app has been running for (15013 ms in this example),  6220 and the number of unique keys written into the database so far (6220 keys in this example). Keys after that upto 6225 are currently being worked on.

```sh
Uptime: 15013 ms | maxWrittenKey: 6220 | maxGeneratedKey: 6225
```


### 6. Verify using cqlsh

You can inspect the table created by the sample app using cqlsh. Connect to the local cluster [as before](../test-cassandra/). The sample app creates the `cassandrakeyvalue` table in the keyspace `ybdemo_keyspace`.

```{.sql .copy .separator-gt}
cqlsh> use ybdemo_keyspace;
```
```{.sql .copy .separator-gt}
cqlsh:ybdemo_keyspace> DESCRIBE cassandrakeyvalue;
```
```{.sql .copy .separator-gt}
cqlsh:ybdemo_keyspace> CREATE TABLE ybdemo_keyspace.cassandrakeyvalue (
    k text PRIMARY KEY,
    v blob
) WITH default_time_to_live = 0;
```


You can select a few rows and examine what gets written.

```{.sql .copy .separator-gt}
cqlsh:ybdemo_keyspace> SELECT * FROM cassandrakeyvalue LIMIT 5;
```
```sql
 k                                         | v
-------------------------------------------+--------------------
 19870fdc-bb5f-4d31-8afa-fc6c60153c28:1508 | 0x76616c3a31353038
  19870fdc-bb5f-4d31-8afa-fc6c60153c28:188 |   0x76616c3a313838
 19870fdc-bb5f-4d31-8afa-fc6c60153c28:2550 | 0x76616c3a32353530
 19870fdc-bb5f-4d31-8afa-fc6c60153c28:2306 | 0x76616c3a32333036
  19870fdc-bb5f-4d31-8afa-fc6c60153c28:553 |   0x76616c3a353533

(5 rows)
```


## Key-Value Workload using Redis API

### 1. Run the app

You can run the Redis sample app `RedisKeyValue` using the following command.

```{.sh .copy .separator-dollar}
$ java -jar yb-sample-apps.jar --workload RedisKeyValue --nodes localhost:6379 --nouuid --num_threads_read 24 --num_threads_write 1
```


It starts multiple writer and reader threads to generate the load. Below is a short description of the output logs, which will help in understanding what the workload is doing.


### 2. Configure number of reader and writer threads.

The number of reader and writer threads can be tuned in the sample app using the `--num_threads_read <num>` and the `--num_threads_write <num>` flags.

```sh
2017-10-29 14:25:48,728 [INFO|...|CmdLineOpts] Num reader threads: 24, num writer threads: 1
```

This log line prints out how many threads are performing reads and writes. By default, 24 threads perform 1 outstanding read each and 1 thread performs 1 outstanding write.


### 3. Review runtime read and write stats

The sample app prints out its read and write stats every 5 seconds. The log lines would look like the following.

```sh
Read: 4971.71 ops/sec (4.83 ms/op), 72404 total ops  |  Write: 125.98 ops/sec (7.93 ms/op), 1808 total ops
Read: 5123.62 ops/sec (4.67 ms/op), 98037 total ops  |  Write: 129.13 ops/sec (7.72 ms/op), 2454 total ops
```


The first portion of each log line (shown below) says that the sample app is currently doing around 5K read ops/sec, with an average 4.67ms latency per op. It has done around 98K read ops so far using 24 (or the specified number) of reader threads.

```sh
Read: 5123.62 ops/sec (4.67 ms/op), 98037 total ops
```

The second portion of the log line (again shown below) says that the sample app is currently doing 129 writes/sec, each taking 7.72ms. It has done about 2K ops total thus far, using 1 (or speficied number of) writer threads. Note that if you are running the default configuration, each write has a replication factor of 3 and each write persists the data upon a success like a true database would.

```sh
Write: 129.13 ops/sec (7.72 ms/op), 2454 total ops
```

### 4. Verify using redis-cli

```{.sh .copy .separator-dollar}
$ ./bin/redis-cli
```
```{.sh .copy .separator-gt}
127.0.0.1:6379> get key:1  
```
```sh
"val:1"  
```
```{.sh .copy .separator-gt}
127.0.0.1:6379> get key:2  
```
```sh
"val:2"
```
```{.sh .copy .separator-gt}
127.0.0.1:6379> get key:1000 
```
```sh
"val:1000"  
```
