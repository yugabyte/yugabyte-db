---
title: Change data capture to stdout
linkTitle: Change data capture (CDC) to stdout
description: Change data capture (CDC) to stdout
beta: /faq/product/#what-is-the-definition-of-the-beta-feature-tag
menu:
  latest:
    parent: cdc
    identifier: cdc-to-stdout
    weight: 692
type: page
isTocNested: true
showAsideToc: true
---

[Change data capture (CDC)](../architecture/cdc-architecture) in YugabyteDB applications can be used to asynchronously replicate data changes from a YugabyteDB cluster to the `stdout` stream. The data changes in YugabyteDB are detected, captured, and then output to a specified target. In the steps below, you can use a local YugabyteDB cluster to use the Change Data Capture (CDC) API to send data changes to `stdout`. To learn about the change data capture (CDC) architecture, see [Change data capture (CDC)](../architecture/cdc-architecture).

## Prerequisites

### YugabyteDB

A 1-node YugabyteDB cluster with RF=1 is up and running locally (the `yb-ctl create` command create this by default). If you are new to YugabyteDB, you can create a local YugaByte cluster in under five minutes by following the steps in the [Quick start](/quick-start/install/).

### Java

A JRE (or JDK), for Java 8 or later, is installed. JDK and JRE installers for Linux, macOS, and Windows can be downloaded from [OpenJDK](http://jdk.java.net/), [AdoptOpenJDK](https://adoptopenjdk.net/), or [Azul Systems](https://www.azul.com/downloads/zulu-community/).

## Step 1 — Add a database table

Start your local YugabyteDB cluster and add a table (for example, `cdc` to the default `yugabyte` database.

## Step 2 — Download the Yugabyte CDC connector

Download the [Yugabyte CDC connector (JAR file)](https://github.com/yugabyte/yb-kafka-connector/blob/master/yb-cdc/yb-cdc-connector.jar).

## Step 3 — Stream the log output stream to `stdout`

You can now follow the command below to to start the YugabyteDB CDC connector and stream the output from the `cdc` table to `stdout`.

```bash
java -jar yb_cdc_connector.jar
--table_name yugabyte.cdc
--log_only // Flag to log to console.
```

## Step 4 — Write values and observe

In another terminal shell, write some values to the table and observe the values on your `stdout` output stream.
