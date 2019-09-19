---
title: Use change data capture (CDC)
linkTitle: Use change data capture (CDC)
description: Use change data capture (CDC)
beta: /faq/product/#what-is-the-definition-of-the-beta-feature-tag
menu:
  latest:
    parent: cdc
    identifier: use-cdc
    weight: 691
type: page
isTocNested: true
showAsideToc: true
---


## Download the Yugabyte CDC connector

Download the [Yugabyte CDC connector (JAR file)](https://github.com/yugabyte/yb-kafka-connector/blob/master/yb-cdc/yb-cdc-connector.jar).

## Use the Yugabyte CDC connector

To use the Yugabyte CDC connector, run the `yb_cdc_connector` JAR file with any 

```bash
java -jar yb_cdc_connector.jar
--table_name <database>.<table>
--master_addrs <yb master addresses> [default 127.0.0.1:7100]
--[stream_id] <optional existing stream id>
--log_only // Flag to log to console.
```

## Parameters

### Required parameters

#### `--table_name`

Specify the name of the YSQL database or YCQL namespace.

#### `--master_addrs`

Specify the IP address of the YB-Master. Default value is `127.0.0.1:7100`.

If you are using a 3-node local cluster, then you need to specify a comma-delimited list of the addresses for all of your YB-Master services.

### Optional parameters

#### `--stream_id`

Specify the existing stream ID. If you do not specify the stream ID, on restart the log output stream starts from the first available record.

If specified (recommended), on restart, the log output stream resumes after the last output logged.

To get the stream ID, the first time you can get the stream ID from the console output.

#### `--log_only`

Flag to restrict logging only to the console.

## Examples

