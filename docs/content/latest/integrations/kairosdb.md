---
title: KairosDB
linkTitle: KairosDB
description: KairosDB
aliases:
section: INTEGRATIONS
menu:
  latest:
    identifier: kairosdb
    weight: 574
isTocNested: true
showAsideToc: true
---

[KairosDB](http://kairosdb.github.io/) is a Java-based time-series metrics API that leverages Cassandra as its underlying distributed database. This page shows how it can be integrated with YugabyteDB's Cassandra-compatible YCQL API.

## Prerequisites

Before you can start using the KairosDB plugin, ensure that you have:

- Java version 1.8 or later.
- The latest version of [KairosDB](https://kairosdb.github.io/docs/GettingStarted.html).
- The latest version of [YugabyteDB plugin for KairosDB](https://github.com/yugabyte/kairosdb-yb-plugin/releases/) and explore the [README] for details about the plugin.
- A YugabyteDB cluster. Refer [YugabyteDB Quick Start Guide](/latest/quick-start/) to install and start a local cluster.
- [Postman API Platform](https://www.postman.com/downloads/).
- (Optional) YugabyteDB's [cassandra-driver-core-3.10.3-yb-2.jar](https://repo1.maven.org/maven2/com/yugabyte/cassandra-driver-core/3.10.3-yb-2/cassandra-driver-core-3.10.3-yb-2.jar), for better performance.

## Start KairosDB

- Copy the YugabyteDB plugin jar under the `lib` folder of your downloaded `kairosdb` directory.
- (Optional) For better performance, replace the `cassandra-driver-core-3.10.2.jar` with YugabyteDB's `cassandra-driver-core-3.10.3-yb-2.jar` in your `kairosdb/lib` directory.
- Add YugabyteDB datastore as the `service.datastore` entry in your `kairosdb/conf/kairosdb.conf file`.

```sh
#Configure the datastore

#service.datastore: "org.kairosdb.datastore.h2.H2Module"
#service.datastore: "org.kairosdb.datastore.cassandra.CassandraModule"
service.datastore: "com.yugabyte.kairosdb.datastore.yugabytedb.YugabyteDBModule"
```

- Start KairosDB in the foreground.

```sh
$ ./bin/kairosdb.sh run
```

```output
18:34:01.094 [main] INFO  [AbstractConnector.java:338] - Started SelectChannelConnector@0.0.0.0:8080
18:34:01.098 [main] INFO  [Main.java:522] - Starting service class org.kairosdb.core.telnet.TelnetServer
18:34:01.144 [main] INFO  [Main.java:378] - ------------------------------------------
18:34:01.144 [main] INFO  [Main.java:379] -      KairosDB service started
18:34:01.145 [main] INFO  [Main.java:380] - ------------------------------------------
```

The KairosDB API Server should be available at `localhost:8080`.

## Verify the integration with ycqlsh

- Run ycqlsh to connect and use YugabyteDB's YCQL API.

```sh
$ ./bin/ycqlsh localhost
```

```output
Connected to local cluster at 127.0.0.1:9042.
[ycqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
ycqlsh>
```

- Run the following YCQL commands, and connect to the `kairosdb` keyspace to verify it is working:

```cql
ycqlsh> describe keyspaces;
```

```output
kairosdb  system_schema  system_auth  system
```

```cql
ycqlsh> use kairosdb;
```

```cql
ycqlsh:kairosdb> describe tables;
```

```output
tag_indexed_row_keys  row_key_index  service_index  row_key_time_index
row_keys              data_points    string_index   spec
```

## Test KairosDB

- Start your postman and create a new workspace.

### Push data

- Select the POST API request and add the following URL in the `Enter request URL` box:

```text
http://localhost:8080/api/v1/datapoints
```

- In the body of the request, add the following JSON and send it.

```json
[
  {
      "name": "archive_file_tracked",
      "datapoints": [[1359788400000, 123], [1359788300000, 13.2], [1359788410000, 23.1]],
      "tags": {
          "host": "server1",
          "data_center": "DC1"
      },
      "ttl": 300.0
  },
  {
      "name": "archive_file_search",
      "timestamp": 1359786400000,
      "value": 321,
      "tags": {
          "host": "server2"
      }
  }
]
```

Your request should look like:


Your response should return a status code as 204, but no body.



### Query the data

- Query the data you inserted during the push using the POST API with the following URL in the `Enter request URL` box:

```text
http://localhost:8080/api/v1/datapoints/query
```

- In the body of the request add the following JSON, and send it.

```json
{
  "start_absolute":1359788400000,
  "metrics":[
    {
      "tags":{
        "host":"server1",
        "data_center":"DC1"
      },
      "name":"archive_file_tracked"
    }
  ]
}
```

Your request should look like:

Your response should return a status code as 200, with the following output:

```output.json
{
   "queries": [
       {
           "sample_size": 2,
           "results": [
               {
                   "name": "archive_file_tracked",
                   "group_by": [
                       {
                           "name": "type",
                           "type": "number"
                       }
                   ],
                   "tags": {
                       "data_center": [
                           "DC1"
                       ],
                       "host": [
                           "server1"
                       ]
                   },
                   "values": [
                       [
                           1359788400000,
                           123
                       ],
                       [
                           1359788410000,
                           23.1
                       ]
                   ]
               }
           ]
       }
   ]
}
```
