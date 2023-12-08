---
title: KairosDB
linkTitle: KairosDB
description: Use KairosDB with YCQL API
aliases:
menu:
  preview_integrations:
    identifier: kairosdb
    parent: integrations
    weight: 571
type: docs
---

[KairosDB](http://kairosdb.github.io/) is a Java-based time series metrics API that leverages Cassandra as its underlying distributed database. This page shows how it can be integrated with YugabyteDB's Cassandra-compatible YCQL API.

## Prerequisites

Before you start using the KairosDB plugin, ensure that you have:

- Java version 1.8 or later.
- The latest version of [KairosDB](https://kairosdb.github.io/docs/GettingStarted.html).
- The latest version of [YugabyteDB plugin for KairosDB](https://github.com/yugabyte/kairosdb-yb-plugin/releases/); read the README for details about the plugin.
- A YugabyteDB cluster. Refer to [YugabyteDB Quick start guide](/preview/quick-start/) to install and start a local cluster.
- [Postman API Platform](https://www.postman.com/downloads/).
- (Optional) YugabyteDB [cassandra-driver-core-3.10.3-yb-2.jar](https://repo1.maven.org/maven2/com/yugabyte/cassandra-driver-core/3.10.3-yb-2/cassandra-driver-core-3.10.3-yb-2.jar), for better performance.

## Configure and start KairosDB

To configure KairosDB, do the following:

- Copy the YugabyteDB plugin for KairosDB jar to the `lib` folder of your downloaded `kairosdb` directory.
- Optionally, for better performance, replace the `cassandra-driver-core-3.10.2.jar` with the YugabyteDB `cassandra-driver-core-3.10.3-yb-2.jar` in the `kairosdb/lib` directory.
- Add YugabyteDB datastore as the `service.datastore` entry in your `kairosdb/conf/kairosdb.conf file`.

    ```sh
    #Configure the datastore

    #service.datastore: "org.kairosdb.datastore.h2.H2Module"
    #service.datastore: "org.kairosdb.datastore.cassandra.CassandraModule"
    service.datastore: "com.yugabyte.kairosdb.datastore.yugabytedb.YugabyteDBModule"
    ```

To start KairosDB in the foreground, do the following:

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

The KairosDB API server should be available at `localhost:8080`.

## Verify the integration using ycqlsh

Run [ycqlsh](/preview/admin/ycqlsh/) to connect to your database using the YCQL API as follows:

```sh
$ ./bin/ycqlsh localhost
```

```output
Connected to local cluster at 127.0.0.1:9042.
[ycqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
ycqlsh>
```

Connect to the `kairosdb` keyspace to verify it is working as follows:

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

Launch Postman via the app or web and create a new workspace from the homepage.

![KairosDB workspace](/images/develop/ecosystem-integrations/kairosdb/kairosdb-workspace.png)

Select the workspace and click the `+` button to create an HTTP request.

![KairosDB request](/images/develop/ecosystem-integrations/kairosdb/kairosdb-http-request.png)

### Push data

Select the POST API request in the dropdown as follows:

![KairosDB request type](/images/develop/ecosystem-integrations/kairosdb/kairosdb-request-type.png)

Add the following URL in the `Enter request URL` box:

```text
http://localhost:8080/api/v1/datapoints
```

In the body of the request, add the following JSON, then click **Send**.

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

![KairosDB POST request](/images/develop/ecosystem-integrations/kairosdb/kairosdb-request1.png)

Your response should return a status code of 204 with no body.

![KairosDB response](/images/develop/ecosystem-integrations/kairosdb/kairosdb-response.png)

### Query the data

To query the data you [inserted using the POST API](#push-data), enter the following URL in the `Enter request URL` box:

```text
http://localhost:8080/api/v1/datapoints/query
```

In the body of the request, add the following JSON, and send it.

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

![kairosdb3](/images/develop/ecosystem-integrations/kairosdb/kairosdb-request2.png)

Your response should return a status code of 200, with the following output:

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
