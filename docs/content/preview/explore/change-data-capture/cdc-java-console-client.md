---
title: Java console client for CDC
headerTitle: Use the Java CDC console client
linkTitle: Java CDC console
description: Use the CDC console client to learn how CDC works.
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  preview:
    parent: explore-change-data-capture
    identifier: cdc-java-console-client
    weight: 580
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../cdc-java-console-client/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
</ul>

The Java console client for CDC prints CDC changes to the console. This client is meant **for testing purposes only**.

## Set up YugabyteDB

This can be done either locally using yugabyted or via YugabyteDB Managed. Refer to the [Quick Start](../../../quick-start/) guide to learn more.

1. If you're testing locally, create a cluster using `yugabyted`.

    ```sh
    ./bin/yugabyted start
    ```

1. Next, connect to your cluster using the YSQL shell:

    ```sh
    ./bin/ysqlsh
    ```

1. Once connected, create a database.

    \
    You can use any database, as long as you have the permission for it. In this example, you create `testdatabase`.

    ```sql
    CREATE DATABASE testdatabase;
    \c testdatabase
    CREATE TABLE test (id int primary key, name varchar(50), email text);
    ```

1. Create a CDC stream.

    \
    Use the `create_change_data_stream` command to create a stream. For a full list of CDC commands, see the [yb-admin](../../../admin/yb-admin/#change-data-capture-cdc-commands) page.

    ```sh
    ./bin/yb-admin create_change_data_stream ysql.yugabyte
    ```

    ```output
    CDC Stream ID: 0bb74adc723248d584ce90b856974633
    ```

## Run the console client

1. Create a configuration file.

    \
    Create a file called `config.properties`, with the following contents. Replace the `stream.id` value with the CDC stream ID from the previous step.

    ```conf
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

1. Run the Java console client, and you can directly view the changes in your terminal.

    ```bash
    java -jar java/yb-cdc/target/yb-cdc-connector.jar --config_file config.properties
    ```

You can also specify the following optional flags:

| Flag | Default value | Description |
| :--- | :------------ | :---------- |
| `--help` | n/a | Display the help. |
| `--show_config_file` | n/a | Display an example of a config.properties file. |
| `--master_address` | n/a | Comma-separated list of master nodes in the form `host:port`. |
| `--table_name` | n/a | Table name. This can take two formats:<ul><li>Use `<namespace-name>.<table-name>` if the schema of the table is public. <li>Use `<namespace-name>.<schema-name>.<table-name>` if the table is under any other schema.</ul>|
| `--stream_id` | n/a | Stream ID created using yb-admin. |
| `--disable_snapshot` | n/a | Disable taking a snapshot of the table. By default, the console client always takes a snapshot of the table. |
| `--ssl_cert_file` | n/a | Root certificate against which the server is to be validated. |
| `--ssl_client_cert` | n/a | Path to client certificate file. |
| `--ssl_client_key` | n/a | Path to client private key file. |
| `--max_tablets` | 10 | Maximum number of tablets the client can poll for. This should be greater than or equal to the number of tablets for the table. |
| `--poll_interval` | 200 | Polling interval at which the client should request for the changes, in milliseconds. |
| `--create_new_db_stream_id` | n/a | Automatically create a database stream ID. In general, you should use yb-admin rather than this flag. |
