---
title: Deploy to two data centers with asynchronous replication
headerTitle: Asynchronous Replication
linkTitle: Asynchronous Replication
description: Enable deployment using unidirectional (master-follower) or bidirectional (multi-master) replication between data centers
menu:
  latest:
    parent: multi-dc
    identifier: async-replication
    weight: 633
aliases:
type: page
isTocNested: true
showAsideToc: true
---

You can perform deployment using unidirectional (master-follower) or bidirectional (multi-master) asynchronous replication between data centers.

For information on two data center (2DC) deployment architecture and supported replication scenarios, see [Two data center (2DC) deployments](../../../architecture/2dc-deployments).

## Setting Up Universes

You can create producer and consumer universes as follows:

1. Create the yugabyte-producer universe by following the procedure from [Manual deployment](../../manual-deployment).
1. Create tables for the APIs being used by the producer.
1. Create the yugabyte-consumer universe by following the procedure from [Manual deployment](../../manual-deployment).
1. Create tables for the APIs being used by the consumer. These should be the same tables as you created for the producer universe.
1. Proceed to setting up [unidirectional](#seting-up-unidirectional-replication) or [bidirectional](#setting-up-bidirectional-replication) replication.

## Setting Up Unidirectional Replication

After you've created the required tables, you can set up asysnchronous replication as follows:

- Look up the producer universe UUID and the table IDs for the two tables and the index table on the master UI:

    - To find a universe's UUID in Yugabyte Platform, open the **Universes** tab, click the name of the universe and notice the URL of the universe's **Overview** page that ends with the universe's UUID (for example, `http://myPlatformServer/universes/d73833fc-0812-4a01-98f8-f4f24db76dbe`). 
    - To find a table's UUID in Yugabyte Platform, click the **Tables** tab on the universe's **Overview** page, then click the table and copy its UUID from its **Overview** tab.

- Run the following `yb-admin` [`setup_universe_replication`](../../../admin/yb-admin/#setup-universe-replication) command from the YugabyteDB home directory in the producer universe:

    ```sh
    ./bin/yb-admin \
      -master_addresses <consumer_universe_master_addresses> \
      setup_universe_replication <producer_universe_uuid> \
        <producer_universe_master_addresses> \
        <table_id>,[<table_id>..]
    ```

    For example:

    ```sh
    ./bin/yb-admin \
      -master_addresses 127.0.0.11:7100,127.0.0.12:7100,127.0.0.13:7100 \
      setup_universe_replication e260b8b6-e89f-4505-bb8e-b31f74aa29f3 \
        127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
        000030a5000030008000000000004000,000030a5000030008000000000004005,dfef757c415c4b2cacc9315b8acb539a
    ```


The preceding command contains three table IDs: the first two are YSQL for the base table and index, and the third is the YCQL table. 

Also, be sure to specify all master addresses for producer and consumer universes in the command.

If you need to set up bidirectional replication, see instructions in provided in [Setting Up Bidirectional Replication](#setting-up-bidirectional-replication). Otherwise, proceed to [Loading Data into the Producer Universe](#loading-data-into-the-producer-universe).

## Setting Up Bidirectional Replication

To set up bidirectional replication, repeat the procedure descried in [Setting Up Unidirectional Replication](#setting-up-unidirectional-replication) applying the steps to the yugabyte-consumer universe. You need to set up each yugabyte-producer to consume data from yugabyte-consumer.

When completed, proceed to [Loading Data](#loading-data-into-the-producer-universe).

## Loading Data into the Producer Universe

Once you have set up replication, load data into the producer universe as follows:

- Download the YugabyteDB workload generator JAR file `yb-sample-apps.jar` from [GitHub](https://github.com/yugabyte/yb-sample-apps/releases).

- Start loading data into yugabyte-producer by following examples for YSQL or YCQL:

  - YSQL:

  ```sh
  java -jar yb-sample-apps.jar --workload SqlSecondaryIndex  --nodes 127.0.0.1:5433
  ```

  - YCQL:

  ```sh
  java -jar yb-sample-apps.jar --workload CassandraBatchKeyValue --nodes 127.0.0.1:9042
  ```

- For bidirectional replication, repeat the preceding step in the yugabyte-consumer universe.

When completed, proceed to [Verifying Replication](#verifying-replication).

## Verifying Replication

You can verify whether or not replication has been successful.

### Unidirectional Replication

For unidirectional replication, connect to the yugabyte-consumer universe using the YSQL shell (`ysqlsh`) or the YCQL shell (`ycqlsh`), and confirm that you can see the expected records.

### Bidirectional Replication

For bidirectional replication, repeat the procedure described in [Unidirectional Replication](#unidirectional-replication), but pump data into yugabyte-consumer. 

To avoid primary key conflict errors, keep the key space for the two universes separate.

### Replication Lag

Replication lag is computed at the tablet level as follows:

`replication lag = hybrid_clock_time - last_read_hybrid_time`

*hybrid_clock_time* is the hybrid clock timestamp on the producer's tablet-server, and *last_read_hybrid_time* is the hybrid clock timestamp of the latest transaction pulled from the producer.

An example script [`determine_replication_lag.sh`](/files/determine_replication_lag.sh) calculates the replication lag for you. The script requires the [`jq`](https://stedolan.github.io/jq/) package.

The following example generates a replication lag summary for all tables on a cluster. You can also request an individual table.

```sh
./determine_repl_latency.sh -m 10.150.255.114,10.150.255.115,10.150.255.113
```

To obtain a summary of all command options, call `determine_repl_latency.sh -h` .
