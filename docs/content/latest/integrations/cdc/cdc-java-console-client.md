---
title: Java console client for CDC
headerTitle: Using Java console client for testing CDC locally
linkTitle: Java CDC console
description: Use the CDC console client to learn how CDC works.
menu:
  latest:
    identifier: cdc-java-console
    parent: cdc
    weight: 580
isTocNested: true
showAsideToc: true
---

The Java console client for CDC prints CDC changes to the console. This client is meant **for testing purposes only**.

## Start a cluster

This can be done either locally using yugabyted or via Yugabyte Cloud. Take a look at the [Quick Start](https://docs.yugabyte.com/latest/quick-start/) guide to know more.

If you're testing locally, create a cluster using `yugabyted`.

```sh
./bin/yugabyted start
```

## Connect to ysqlsh

```sh
./bin/ysqlsh
```

Once connected, create a database. You can use any database, as long as you have the permission for it. In this example, you create `testdatabase`.

```sql
CREATE DATABASE testdatabase;
\c testdatabase
CREATE TABLE test (id int primary key, name varchar(50), email text);
```

## Create a CDC stream

Use the `create_change_data_stream` command to create a stream. For a full list of CDC commands, see the [yb-admin](../../admin/yb-admin.md#change-data-capture-cdc-commands) page.

```sh
./bin/yb-admin create_change_data_stream ysql.yugabyte
```

```output
CDC Stream ID: 0bb74adc723248d584ce90b856974633
```

## Create a configuration file

Create a file called `config.properties`, with the following contents. Replace the `stream.id` value with the CDC stream ID from the previous step.

```properties
admin.operation.timeout.ms=30000
operation.timeout.ms=30000
num.io.threads=1
socket.read.timeout.ms=30000
table.name=test
stream.id=0bb74adc723248d584ce90b856974633
schema.name=testdatabase
format=proto
master.address=127.0.0.1:7100
```

## Run the JAR file

Run the Java console client, and you can directly view the changes in your terminal.

```bash
java -jar java/yb-cdc/target/yb-cdc-connector.jar --config_file config.properties
```

You can also specify the following optional flags:

| Flag | Default value | Description |
| :--- | :------------ | :---------- |
| --help | n/a | Display the help. |
| --show_config_file | n/a | Display an example of a config.properties file. |
| --master_address | n/a | Comma-separated list of master nodes in the form `host:port`. |
| --table_name | n/a | Table name. This can take two formats:<br/><br/> Use `<namespace-name>.<table-name>` if the schema of the table is public. <br/><br/> Use `<namespace-name>.<schema-name>.<table-name>` if the table is under any other schema. |
| --stream_id | n/a | Stream ID created using yb-admin. |
| --disable_snapshot | n/a | Disable taking a snapshot of the table. By default, the console client always takes a snapshot of the table. |
| --ssl_cert_file | n/a | Root certificate against which the server is to be validated. |
| --ssl_client_cert | n/a | Path to client certificate file. |
| --ssl_client_key | n/a | Path to client private key file. |
| --max_tablets | 10 | Maximum number of tablets the client can poll for. This should be greater than or equal to the number of tablets for the table. |
| --poll_interval | 200 | Polling interval at which the client should request for the changes, in milliseconds. |
| --create_new_db_stream_id | n/a | Automatically create a database stream ID. In general, you should use yb-admin rather than this flag. |
