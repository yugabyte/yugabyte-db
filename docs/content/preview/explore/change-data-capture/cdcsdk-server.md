---
title: CDCSDK Server
headerTitle: CDCSDK Server
linkTitle: CDCSDK Server
description: A ready to use application used to capture the changes in a database.
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
aliases:
  - /preview/explore/change-data-capture/cdcsdk-server-ysql
  - /preview/explore/change-data-capture/cdcsdk-server-debezium
  - /preview/explore/change-data-capture/debezium-server-yugabyte
  - /preview/explore/change-data-capture/cdcsdk-server
menu:
  preview:
    parent: change-data-capture
    identifier: cdcsdk-server
    weight: 580
isTocNested: true
showAsideToc: true
---

CDCSDK server is Yugabyte's implementation of the Debezium Server specifically crafted to work with cloud deployments. It's a ready to use application for streaming changes from databases to messaging infrastructures.

### On this page

* [Installation](#installation)
* [Configuration](#configuration)
* [HTTP Client](#http-client)
* [Health checks](#health-checks)

### Installation

CDCSDK Server distribution archives are available in [Github Releases](https://github.com/yugabyte/cdcsdk-server/releases) of the project. Each of the releases has a tar.gz labelled as CDCSDK Server.

The archive has the following layout:

```output
cdcsdk-server
  |--CHANGELOG.md
  |-- conf
  |-- CONTRIBUTE.md
  |-- COPYRIGHT.txt
  |-- debezium-server-1.9.2.Final-runner.jar
  |-- lib
  |-- LICENSE-3rd-PARTIES.txt
  |-- LICENSE.txt
  |-- README.md
  |-- run.sh
```

#### Unpack and run instructions

```sh
export CDCSDK_VERSION=<x.y.z>
wget https://github.com/yugabyte/cdcsdk-server/releases/download/v${CDCSDK_VERSION}/cdcsdk-server-dist-${CDCSDK_VERSION}.tar.gz

# Or if you are using GitHub CLI
gh release download v{CDCSDK_VERSION} -A tar.gz --repo yugabyte/cdcsdk-server

tar xvf cdcsdk-server-dist-${CDCSDK_VERSION}.tar.gz
cd cdcsdk-server

# Configure the application. More details in the next section
touch conf/application.properties

# Run the application
./run.sh
```

### Configuration

The main configuration file is `conf/application.properties`. There are multiple sections configured:
* `debezium.source`: is for source connector configuration. Each instance of Debezium Server runs exactly one connector.
* `debezium.sink` is for the sink system configuration.
* `debezium.format` is for the output serialization format configuration.
* `debezium.transforms` is for the configuration of message transformations.

**Example:**

```properties
debezium.sink.type=kafka
debezium.sink.kafka.producer.bootstrap.servers=127.0.0.1:9092
debezium.sink.kafka.producer.key.serializer=org.apache.kafka.common.serialization.StringSerializer
debezium.sink.kafka.producer.value.serializer=org.apache.kafka.common.serialization.StringSerializer
debezium.source.connector.class=io.debezium.connector.yugabytedb.YugabyteDBConnector
debezium.source.database.hostname=127.0.0.1
debezium.source.database.port=5433
debezium.source.database.user=yugabyte
debezium.source.database.password=yugabyte
debezium.source.database.dbname=yugabyte
debezium.source.database.server.name=dbserver1
debezium.source.database.streamid=de362081fa864e94b35fcac6005e7cd9
debezium.source.table.include.list=public.test
debezium.source.database.master.addresses=127.0.0.1:7100
debezium.source.snapshot.mode=never
```

The `debezium.source` configurations are nothing but the Debezium Connector's configurations only where you can specify the `debezium.source` as a prefix to any of the connector's configurations. For a complete list of the Debezium configurations, see [Debezium Connector for YugabyteDB configurations](../change-data-capture/debezium-connector-yugabytedb.md#connector-configuration-properties).

<!-- TODO Vaibhav: add more configuration examples -->

#### Configuration using HTTP client

Configuration using environment variables maybe useful when running in containers. The rule of thumb is to convert the keys to UPPER CASE and replace `.` with `_`. For example, `debezium.source.database.port` has to be changed to `DEBEZIUM_SOURCE_DATABASE_PORT`.

### HTTP Client

The HTTP Client will stream changes to any HTTP Server for additional processing with the original design goal to make Debezium act as a native event source.

| Property | Default | Description |
| :---- | :---- | :---- |
| `debezium.sink.type` | | Must be set to `http` |
| `debezium.sink.http.url` | | The HTTP Server URL to stream events to. This can also be set by defining the K_SINK environment variable, which is used by the Knative source framework. |
| `debezium.sink.http.timeout.ms` | 60000 | The number of milli-seconds to wait for a response from the server before timing out. |

### Health checks

CDCSDK Server exposes a simple health check REST API. Currently the health check only ensures that the server is up and running.

#### Running the health checks

The following REST endpoints are exposed:

* `/q/health/live` - The application is up and running.
* `/q/health/ready` - The application is ready to serve requests.

All of the health REST endpoints return a simple JSON object with two fields:
* `status` — The overall result of all the health check procedures.
* `checks` — An array of individual checks.

The general status of the health check is computed as a logical AND of all the declared health check procedures. The checks array is currently empty as we have not specified any health check procedure yet.

**Example:**

```output
curl http://localhost:8080/q/health/live

{
    "status": "UP",
    "checks": [
        {
            "name": "debezium",
            "status": "UP"
        }
    ]
}

curl http://localhost:8080/q/health/ready

{
    "status": "UP",
    "checks": [
    ]
}
```