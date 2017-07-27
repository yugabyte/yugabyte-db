---
date: 2016-03-09T00:11:02+01:00
title: Community Edition - Start a local node
weight: 10
---

Getting started with YugaByte in a developer's localhost environment is easy. Run the single-node YugaByte docker container with the instructions below and you will have a Apache Cassandra as well as a Redis server running on your localhost at their respective default ports (9042 for CQL and 6379 for Redis).

Note that this **local node** approach simply provides API endpoints to develop against. It can neither be monitored nor can be scaled out to a multi-node cluster. For all such operational needs, refer to the [Local cluster] (/get-started/local-cluster/) or [Deploy](/deploy/) sections.

## Prerequisites

### Docker

#### Mac OS or Windows Desktop

- Install [Docker for Mac](https://docs.docker.com/docker-for-mac/install/) or [Docker for Windows](https://store.docker.com/editions/community/docker-ce-desktop-windows). Please check carefully that all prerequisites are met.

- Confirm that the Docker daemon is running in the background. If you don't see the daemon running, start the Docker application

```sh
$ docker version
```

#### Linux

{{< note title="Note" >}}
Docker for Linux requires sudo privileges. 
{{< /note >}}

- Install [Docker for Linux](https://docs.docker.com/engine/installation/linux/ubuntulinux/). Please check carefully that all prerequisites are met.

- Confirm that the Docker daemon is running in the background. If you don't see the daemon running, start the Docker daemon.

```sh
$ sudo su 
$ docker version
```


## Install

- YugaByte's container images are stored at Quay.io, a leading container registry. Create your free Quay.io account at [Quay.io](https://quay.io/signin/).

- Send email to [YugaByte Support](mailto:support@yugabyte.com) noting your Quay.io username. This is to ensure that the YugaByte DB docker image can be privately shared with you.

- Login to Quay.io from your command line. Detailed instructions [here](https://docs.quay.io/solution/getting-started.html). 

```sh
$ docker login quay.io -u <your-quay-id> -p <your-quay-password>
```

- Start the YugaByte DB (this command will first pull in the container image from Quay.io in case the image is not yet locally available)

```sh
$ docker run -p 9042:9042 -p 6379:6379 -p 7001:7001 -p 9001:9001 --name yugabyte-db --rm -d quay.io/yugabyte/local-db
```


## Connect with cqlsh or redis-cli

### cqlsh

[**cqlsh**](http://cassandra.apache.org/doc/latest/tools/cqlsh.html) is a command line shell for interacting with Apache Cassandra through [CQL (the Cassandra Query Language)](http://cassandra.apache.org/doc/latest/cql/index.html). It is shipped with every Cassandra package, and can be found in the bin/ directory. cqlsh utilizes the Python CQL driver, and connects to the single node specified on the command line.

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

### redis-cli

## Run a sample app


- Verify that Java is installed on your localhost.

```sh
$ java -version
```

- Copy the sample app from the YugaByte DB container onto your localhost

```sh
# copy the executable jar
$ docker cp yugabyte-db:/opt/yugabyte/java/yb-sample-apps.jar /tmp

# copy the source jar
$ docker cp yugabyte-db:/opt/yugabyte/java/yb-sample-apps-sources.jar /tmp
```

### Cassandra sample app

- Run the Cassandra time-series sample app using the executable jar

```sh
$ java -jar /tmp/yb-sample-apps.jar --workload CassandraTimeseries --nodes localhost:9042
```

You will see the following output.

```sh
09:37 $ java -jar /tmp/yb-sample-apps.jar --workload CassandraTimeseries --nodes localhost:9042
2017-06-29 09:37:07,601 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Using a randomly generated UUID : b406bcaa-efd6-4cc4-b13d-2826098bef28
2017-06-29 09:37:07,610 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] App: CassandraTimeseries
2017-06-29 09:37:07,610 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Adding node: localhost:9042
2017-06-29 09:37:07,611 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num reader threads: 1, num writer threads: 16
2017-06-29 09:37:07,614 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num unique keys to insert: 0
2017-06-29 09:37:07,614 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num keys to update: -1
2017-06-29 09:37:07,614 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num keys to read: -1
2017-06-29 09:37:07,614 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Value size: 100
2017-06-29 09:37:07,614 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Table TTL (secs): 86400
2017-06-29 09:37:09,374 [INFO|com.yugabyte.sample.apps.AppBase|AppBase] Ignoring exception dropping table: SQL error (yb/sql/ptree/process_context.cc:41): SQL Error (1.11): Table Not Found - Not found (yb/common/wire_protocol.cc:120): The table does not exist: table_name: "ts_metrics_raw"
namespace {
  name: "ybdemo_keyspace"
}

	ts_metrics_raw;
	^^^^^^^^^^^^^^
 (error -301)
2017-06-29 09:37:11,081 [INFO|com.yugabyte.sample.apps.AppBase|AppBase] Created a Cassandra table using query: [CREATE TABLE IF NOT EXISTS ts_metrics_raw (  user_id varchar, metric_id varchar, node_id varchar, ts timestamp, value varchar, primary key ((user_id, metric_id), node_id, ts)) WITH default_time_to_live = 86400;]
2017-06-29 09:37:12,684 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 61.20 ops/sec (4.42 ms/op), 306 total ops  |  Write: 587.90 ops/sec (6.27 ms/op), 2941 total ops  |  Uptime: 5069 ms | Verification: ON | 
2017-06-29 09:37:17,688 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 318.02 ops/sec (3.14 ms/op), 1898 total ops  |  Write: 2517.02 ops/sec (3.89 ms/op), 15536 total ops  |  Uptime: 10073 ms | Verification: ON | 
2017-06-29 09:37:22,688 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 242.98 ops/sec (4.11 ms/op), 3113 total ops  |  Write: 3128.72 ops/sec (4.53 ms/op), 31181 total ops  |  Uptime: 15073 ms | Verification: ON | 
2017-06-29 09:37:27,690 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 222.93 ops/sec (4.48 ms/op), 4228 total ops  |  Write: 3294.00 ops/sec (4.54 ms/op), 47656 total ops  |  Uptime: 20075 ms | Verification: ON | 
 
```

As you can see above, the Cassandra time-series sample app first creates a keyspace `ybdemo_keyspace` and a table `ts_metrics_raw`. It then starts multiple writer and reader threads to generate the load.

- Verify using cqlsh

```sh
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
$ java -jar /tmp/yb-sample-apps.jar --workload RedisKeyValue --nodes localhost:6379
```

### Source code for the sample apps


```sh
$ jar xf /tmp/yb-sample-apps-sources.jar
```
The above command puts the sample apps Java files in `com/yugabyte/sample/apps` in the current directory.


## Maintain

- Review logs of the YugaByte DB.

```sh
$ docker logs yugabyte-db
```

- Open a bash shell inside the YugaByte DB container.

```sh
$ docker exec -it yugabyte-db bash
```

- Stop the YugaByte DB.

```sh
$ docker stop yugabyte-db
```

- Stop and remove all the container instances

```sh
$ docker rm -f $(docker ps -aq)
```

- Upgrade the YugaByte DB container.

```sh
$ docker pull quay.io/yugabyte/local-db
```