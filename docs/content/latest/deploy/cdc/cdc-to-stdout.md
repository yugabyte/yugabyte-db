---
title: CDC to stdout
linkTitle: CDC to stdout
description: Change data capture (CDC) to stdout
beta: /faq/product/#what-is-the-definition-of-the-beta-feature-tag
menu:
  latest:
    parent: cdc
    identifier: cdc-to-stdout
    weight: 693
type: page
isTocNested: true
showAsideToc: true
---

[Change data capture (CDC)](../architecture/cdc-architecture) in YugabyteDB applications can be used to asynchronously replicate data changes from a YugabyteDB cluster to the `stdout` stream. The data changes in YugabyteDB are detected, captured, and then output to a specified target. In the steps below, you can use a local YugabyteDB cluster to use the Change Data Capture (CDC) API to send data changes to `stdout`. To learn about the change data capture (CDC) architecture, see [Change data capture (CDC)](../architecture/cdc-architecture).

## Prerequisites

### YugabyteDB

A 1-node YugabyteDB cluster with an RF of 1 is up and running locally (the `yb-ctl create` command create this by default). If you are new to YugabyteDB, you can create a local YugabyteDB cluster in under five minutes by following the steps in the [Quick start](/quick-start/install/).

### Java

A JRE (or JDK), for Java 8 or later, is installed. JDK and JRE installers for Linux, macOS, and Windows can be downloaded from [OpenJDK](http://jdk.java.net/), [AdoptOpenJDK](https://adoptopenjdk.net/), or [Azul Systems](https://www.azul.com/downloads/zulu-community/).

## Step 1 — Add a database table

Start your local YugabyteDB cluster and add a table, named `users`, to the default `yugabyte` database.

```postgresql
CREATE TABLE users (name text, pass text, id int, PRIMARY KEY (id));
```

## Step 2 — Download the Yugabyte CDC connector

Download the [Yugabyte CDC connector (JAR file)](https://github.com/yugabyte/yb-kafka-connector/blob/master/yb-cdc/yb-cdc-connector.jar).

## Step 3 — Stream the log output stream to "stdout"

Run the command below to to start the YugabyteDB CDC connector and stream the output from the `cdc` table to `stdout`.

```sh
java -jar yb_cdc_connector.jar
--table_name yugabyte.users
--log_only
```

## Parameters

### Required parameters 

#### `--table_name`

Specify the namespace and table, where namespace is the database (YSQL) or keyspace (YCQL).

#### `--master_addrs`

Specify the IP addresses for all of the YB-Master services that are producing or consuming. Default value is `127.0.0.1:7100`.

If you are using a 3-node local cluster, then you need to specify a comma-delimited list of the addresses for all of your YB-Master services.

#### `--log_only`

Flag to restrict logging only to the console (`stdout`).

### Optional parameters

#### `--stream_id`

Specify the existing stream ID. If you do not specify the stream ID, on restart the log output stream starts from the first available record.

If specified (recommended), on restart, the log output stream resumes after the last output logged.

To get the stream ID, run the YugabyteDB CDC connector and the first time you can get the stream ID from the console output.

For details on the available options, see [Using the Yugabyte CDC connector](./use-cdc).

## Step 4 — Write values and observe

In another terminal shell, write some values to the table and observe the values on your `stdout` output stream.
