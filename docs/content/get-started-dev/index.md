---
date: 2016-03-09T00:11:02+01:00
title: Get started (Development)
weight: 10
---

Getting started with YugaByte in a developer's **localhost** environment is easy. Simply run the YugaByte DB docker container with the instructions below and you will have a Cassandra server running on your localhost's port 9042 (the default CQL port) in no time.

## Prerequisites

### Docker

#### Mac OS

- Install [Docker for Mac](https://docs.docker.com/docker-for-mac/install/). Please check carefully that all prerequisites are met.

- Confirm that the Docker daemon is running in the background. If you don't see the daemon running, start the Docker application

```sh
docker version
```

#### Linux

{{< note title="Note" >}}
Docker for Linux requires sudo privileges. 
{{< /note >}}

- Install [Docker for Linux](https://docs.docker.com/engine/installation/linux/ubuntulinux/). Please check carefully that all prerequisites are met.

- Confirm that the Docker daemon is running in the background. If you don't see the daemon running, start the Docker daemon.

```sh
sudo su 
docker version
```

## Install

- Create a free Docker ID for yourself at [Docker Hub](https://hub.docker.com/), Docker's official hosted registry.

- Send email to [YugaByte Support](mailto:support@yugabyte.com) noting your Docker ID username. This is to ensure that the YugaByte DB docker image can be privately shared with you.

- Login to Docker from your command line. Detailed instructions [here](https://docs.docker.com/engine/reference/commandline/login/). 

```sh
docker login --username samwalton
```

- Pull the YugaByte DB docker image

```sh
docker pull ybadmin/yugabyte-db
```

- Start the YugaByte DB

```sh
docker run -p 9042:9042 -p 7001-7003:7001-7003 -p 9001-9003:9001-9003 --name yugabyte-db --rm -d ybadmin/yugabyte-db
```


## Connect with cqlsh

[**cqlsh**](http://cassandra.apache.org/doc/latest/tools/cqlsh.html) is a command line shell for interacting with Cassandra through [CQL (the Cassandra Query Language)](http://cassandra.apache.org/doc/latest/cql/index.html). It is shipped with every Cassandra package, and can be found in the bin/ directory. cqlsh utilizes the Python CQL driver, and connects to the single node specified on the command line.

- Download latest version of Apache Cassandra: 3.10

```sh
cd /tmp
wget http://mirror.stjschools.org/public/apache/cassandra/3.10/apache-cassandra-3.10-bin.tar.gz 
tar zxvf apache-cassandra-3.10-bin.tar.gz
sudo mv apache-cassandra-3.10 /opt/cassandra
```

- Run cqlsh and connect it to the yugabyte-db container 

```sh
/opt/cassandra/bin/cqlsh localhost
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

system_schema  system  "$$$_DEFAULT"

cqlsh> 

```

## Run a sample app


- Verify that Java is installed on your localhost.

```sh
java -version
```

- Copy the sample app from the YugaByte DB container onto your localhost

```sh
# copy the executable jar
docker cp yugabyte-db:/opt/yugabyte/java/yb-sample-apps.jar /tmp

# copy the source jar
docker cp yugabyte-db:/opt/yugabyte/java/yb-sample-apps-sources.jar /tmp
```

- Run the Cassandra time-series sample app using the executable jar

```sh
java -jar /tmp/yb-sample-apps.jar --workload CassandraTimeseries --nodes localhost:9042
```

You will see the following output.

```sh
15:33 $ java -jar /tmp/yb-sample-apps.jar --workload CassandraTimeseries --nodes localhost:9042
2017-04-26 15:33:10,449 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Using a randomly generated UUID : a73d0655-d497-4f0f-ac47-1f7a09a40cbd
2017-04-26 15:33:10,457 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] App: CassandraTimeseries
2017-04-26 15:33:10,457 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Adding node: localhost:9042
2017-04-26 15:33:10,458 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num reader threads: 1, num writer threads: 16
2017-04-26 15:33:10,459 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num unique keys to insert: 0
2017-04-26 15:33:10,459 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num keys to update: -1
2017-04-26 15:33:10,459 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Num keys to read: -1
2017-04-26 15:33:10,459 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Value size: 100
2017-04-26 15:33:10,460 [INFO|com.yugabyte.sample.common.CmdLineOpts|CmdLineOpts] Table TTL (secs): 86400
log4j:WARN No appenders could be found for logger (com.datastax.driver.core.SchemaParser).
log4j:WARN Please initialize the log4j system properly.
log4j:WARN See http://logging.apache.org/log4j/1.2/faq.html#noconfig for more info.
2017-04-26 15:33:15,539 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 0.00 ops/sec (0.00 ms/op), 0 total ops  |  Write: 0.00 ops/sec (0.00 ms/op), 0 total ops  |  Uptime: 5079 ms | Verification: ON | 
2017-04-26 15:33:16,326 [INFO|com.yugabyte.sample.apps.CassandraTimeseries|CassandraTimeseries] Ignoring exception dropping table: SQL error (yb/sql/ptree/process_context.cc:36): SQL Error (1.11): Table Not Found - Not found (yb/common/wire_protocol.cc:119): The table does not exist: table_name: "ts_metrics_raw"
namespace {
  name: "$$$_DEFAULT"
}

	ts_metrics_raw;
	^^^^^^^^^^^^^^
 (error -301)
2017-04-26 15:33:20,542 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 0.00 ops/sec (0.00 ms/op), 0 total ops  |  Write: 0.00 ops/sec (0.00 ms/op), 0 total ops  |  Uptime: 10082 ms | Verification: ON | 
2017-04-26 15:33:25,544 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 0.00 ops/sec (0.00 ms/op), 0 total ops  |  Write: 0.00 ops/sec (0.00 ms/op), 0 total ops  |  Uptime: 15084 ms | Verification: ON | 
2017-04-26 15:33:26,302 [INFO|com.yugabyte.sample.apps.CassandraTimeseries|CassandraTimeseries] Created a Cassandra table ts_metrics_raw using query: [CREATE TABLE ts_metrics_raw (  user_id varchar, metric_id varchar, node_id varchar, ts timestamp, value varchar, primary key ((user_id, metric_id), node_id, ts)) WITH default_time_to_live = 86400;]
2017-04-26 15:33:30,544 [INFO|com.yugabyte.sample.common.metrics.MetricsTracker|MetricsTracker] Read: 0.00 ops/sec (0.00 ms/op), 0 total ops  |  Write: 0.00 ops/sec (0.00 ms/op), 0 total ops  |  Uptime: 4240 ms | Verification: ON | 
...
```

As you can see above, the Cassandra time-series sample app first creates a table `ts_metrics_raw` and then starts multiple writer and reader threads to generate the load.

- Verify using cqlsh

```sh
cqlsh> use "$$$_DEFAULT";

cqlsh:$$$_DEFAULT> describe tables;

ts_metrics_raw

cqlsh:$$$_DEFAULT> describe ts_metrics_raw;

CREATE TABLE "$$$_DEFAULT".ts_metrics_raw (
    user_id text,
    metric_id text,
    node_id text,
    ts timestamp,
    value text,
    PRIMARY KEY ((user_id, metric_id), node_id, ts)
) WITH CLUSTERING ORDER BY (node_id ASC, ts ASC);

cqlsh:$$$_DEFAULT> select * from ts_metrics_raw limit 10;

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
cqlsh:$$$_DEFAULT>

```

- Review the sample app source code


```sh
jar xf /tmp/yb-sample-apps-sources.jar
```
The above command puts the sample apps Java files in `com/yugabyte/sample/apps` in the current directory.

## Maintain

- Review logs of the YugaByte DB.

```sh
docker logs yugabyte-db 
```

- Open a bash shell inside the YugaByte DB container.

```sh
docker exec -it yugabyte-db bash
```

- Stop the YugaByte DB.

```sh
docker stop yugabyte-db
```
