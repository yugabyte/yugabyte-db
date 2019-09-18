---
title: Change data capture to stdout
linkTitle: Change data capture to stdout
description: Change data capture to stdout
beta: /faq/product/#what-is-the-definition-of-the-beta-feature-tag
menu:
  latest:
    parent: deploy
    identifier: cdc-to-stdout
    weight: 692
type: page
isTocNested: true
showAsideToc: true
---

Yugabyte
You can use the Change Data Capture (CDC) API, you can use YugabyteDB as a data source to an Apache Kafka or console sink.
Follow the steps outlined below to explore the new CDC functionality using a YugabyteDB local cluster.

## Step 1 — Set up YugabyteDB

Create a YugabyteDB local cluster and add a table.

If you are new to YugabyteDB, you can create a local YugaByte cluster in under five minutes by following the steps in the [Quick start](/quick-start/install/

## Step 2 — Set up Kafka Connect to YugabyteDB

1. In a new shell window, fork YugaByte's GitHub repository for [Kafka Connect to YugabyteDB](https://github.com/yugabyte/yb-kafka-connector) and change to the `yb-cdc` directory.

```
git clone https://github.com/yugabyte/yb-kafka-connector.git
cd yb-kafka-connector/yb-cdc
```

2. Start the Kafka connector application.

## Step 3 — Log to `stdout`

You can now follow the steps below to log to `stdout`.

```bash
java -jar target/yb_cdc_connector.jar
--table_name <namespace/database>.<table>
--master_addrs <yb master addresses> [default 127.0.0.1:7100]
--[stream_id] <optional existing stream id>
--log_only // Flag to log to console.
```

## Step 4 — Write values and observe

In another window, write values to the table and observe the values on your chosen output stream.
