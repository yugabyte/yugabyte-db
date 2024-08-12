---
title: Verify migration for YCQL
headerTitle: Verify migration
linkTitle: Verify migration
description: Verify if the migration was successful
badges: ycql
menu:
  preview:
    identifier: verify-migration-ycql
    parent: manage-bulk-import-export
    weight: 740
type: docs
---

{{<api-tabs>}}

To ensure that the migration was successful, you can perform a number of steps.

### Verify database objects

- Verify that all the tables and indexes have been created in YugabyteDB.
- Ensure that triggers and constraints are migrated and are working as expected.

### Run count query in YCQL

In YCQL, the `count()` query can be executed using the [ycrc](https://github.com/yugabyte/yb-tools/tree/main/ycrc) tool.

The tool uses the exposed `partition_hash` function in order to execute smaller, more manageable queries which are individually less resource intensive and tend to not time out.

Set up and run the ycrc tool using the following steps:

1. Download the ycrc tool by compiling the source from the GitHub repository.

1. Run the following command to confirm that the ycrc tool is working:

    ```sh
    ./ycrc --help
    ```

    ```output
    YCQL Row Count (ycrc) parallelizes counting the number of rows in a table for YugabyteDB CQL, allowing count(*) on tables that otherwise would fail with query timeouts

    Usage:

     ycrc <keyspace> [flags]

    Flags:

     -d, --debug  Verbose logging
     -h, --help  help for ycrc
     -c, --hosts strings  Cluster to connect to (default [127.0.0.1])
     -p, --parallel int  Number of concurrent tasks (default 16)
     --password string  user password
     -s, --scale int  Scaling factor of tasks per table, an int between 1 and 10 (default 6)
     --sslca string  SSL root ca path
     --sslcert string  SSL cert path
     --sslkey string  SSL key path
     --tables strings  List of tables inside of the keyspace - default to all
     -t, --timeout int  Timeout of a single query, in ms (default 1500)
     -u, --user string  database user (default "cassandra")
     --verify  Strictly verify SSL host (off by default)
     -v, --version  version for ycrc

    ```

1. Run the ycrc tool to count the rows in a given keyspace using a command similar to the following:

    ```cql
    ./ycrc -c 127.0.0.1 example
    ```

    ```output
    Checking table row counts for keyspace: example
    Checking row counts for: example.sensordata
    Partitioning columns for example.sensordata:(customer_name,device_id)

    Performing 4096 checks for example.sensordata with 16 parallel tasks

    Total time: 261 ms

    ==========
    Total Row Count example.sensordata = 60
    Checking row counts for: example.emp
    Partitioning columns for example.emp:(emp_id)

    Performing 4096 checks for example.emp with 16 parallel tasks

    Total time: 250 ms

    ==========
    Total Row Count example.emp = 3
    ```

    The following is an example with additional flags on the keyspace `example`:

    ```sh
    ./ycrc example \
          -c 127.0.0.1 \                         # Cluster to connect to (default [127.0.0.1])
          -u test \                              # database user (default "cassandra")
          --verify --password xxx \              # user password
          --tables sensordata \                  # List of tables inside of the keyspace - default to all
          -s 7 \                                 # Scaling factor of tasks per table, an int between 1 and 10 (default 6)
          -p 32 \                                # Number of concurrent tasks (default 16)
          -t 3000 \                              # Timeout of a single query, in ms (default 1500)
          --sslca /opt/yugabyte/certs/tests.crt  # This flag needs to specified if client to node authentication is enabled.
    ```
