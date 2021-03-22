---
title: Deploy to two data centers with asynchronous replication
headerTitle: Two data center (2DC)
linkTitle: Two data center (2DC)
description: Set up a 2DC deployment using either unidirectional (master-follower) or bidirectional (multi-master) replication between the data centers.
menu:
  latest:
    parent: multi-dc
    identifier: 2dc-deployment
    weight: 633
aliases:
  - /latest/deploy/replicate-2dc/
type: page
isTocNested: true
showAsideToc: true
---

{{< tip title="Recommended Reading" >}}

[9 Techniques to Build Cloud-Native, Geo-Distributed SQL Apps with Low Latency](https://blog.yugabyte.com/9-techniques-to-build-cloud-native-geo-distributed-sql-apps-with-low-latency/) highlights the various multi-DC deployment strategies (including 2DC deployments) for a distributed SQL database like YugabyteDB.

{{< /tip >}}

For details on the two data center (2DC) deployment architecture and supported replication scenarios, see [Two data center (2DC) deployments](../../../architecture/2dc-deployments).

Follow the steps on this page to set up a 2DC deployment using either unidirectional (master-follower) or bidirectional (multi-master) replication between your data centers.

## Set up your universes

To create your producer and consumer universes, follow these steps:

1. Create the “yugabyte-producer” universe using the [Manual deployment](../../manual-deployment) steps.

1. Create the the tables for the APIs being used by the _producer_.

1. Create the “yugabyte-consumer” universe using the [Manual deployment](../../manual-deployment) steps.

1. Create the tables for the APIs being used by the _consumer_.

    These should be the same tables as you created for the producer universe.

**Next**, set up [unidirectional (master-follower)](#set-up-unidirectional-master-follower-replication) or [bidirectional (multi-master)](#set-up-bidirectional-multi-master-replication) replication.

## Set up unidirectional (master-follower) replication

After you've created the required tables, you can set up asysnchronous replication.

1. Look up the producer universe UUID and the table IDs for the two tables and the index table on the master UI.
    <br/><br/>
    **To find a universe's UUID** in Yugabyte Platform, go to the Universes tab, and click the name of the Universe. The URL of the universe's Overview page ends with the universe's UUID. (For example, `http://myPlatformServer/universes/d73833fc-0812-4a01-98f8-f4f24db76dbe`)
    <br/><br/>
    **To find a table's UUID** in Yugabyte Platform, click the Tables tab on the universe's Overview page. Click the table you want, and copy the table's UUID from its Overview tab.

1. Run the following `yb-admin` [`setup_universe_replication`](../../../admin/yb-admin/#setup-universe-replication) command from the YugabyteDB home directory in the producer universe.

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

    {{< note title="Note" >}}

There are three table IDs in the preceding command: the first two are YSQL for base table and index, and the third is the YCQL table. Also, be sure to specify all master addresses for producer and consumer universes in the command.

    {{< /note >}}

**If you want to set up bidirectional replication**, continue to the [next section](#set-up-bidirectional-multi-master-replication). Otherwise, skip to [Load data into producer universe](#load-data-into-producer-universe).

## Set up bidirectional (multi-master) replication

To set up bidirectional replication, repeat the steps in the [previous section](#set-up-unidirectional-master-follower-replication) for the “yugabyte-consumer” universe. This time, set up each “yugabyte-producer” to consume data from “yugabyte-consumer”.

**Next**, keep reading to load data.

## Load data into the producer universe

Once you've set up replication, do the following to load data into the producer universe:

1. Download the YugabyteDB workload generator JAR file (`yb-sample-apps.jar`) from [GitHub](https://github.com/yugabyte/yb-sample-apps/releases).

1. Start loading data into “yugabyte-producer”.

    **YSQL example**

    ```sh
    java -jar yb-sample-apps.jar --workload SqlSecondaryIndex  --nodes 127.0.0.1:5433
    ```

    **YCQL example**

    ```sh
    java -jar yb-sample-apps.jar --workload CassandraBatchKeyValue --nodes 127.0.0.1:9042
    ```

1. For bidirectional replication, repeat the preceding step in the "yugabyte-consumer" universe.

**Next**, continue to the next section to verify that replication is working correctly.

## Verify replication

**For unidirectional replication**, connect to “yugabyte-consumer” universe using the YSQL shell (`ysqlsh`) or the YCQL shell (`ycqlsh`), and confirm that you can see expected records.

**For bidirectional replication**, repeat the previous step, but pump data into “yugabyte-consumer”. To avoid primary key conflict errors, keep the key space for the two universes separate.

{{< tip title="Checking replication lag" >}}
Replication lag is computed at the tablet level as:

`replication lag = hybrid_clock_time - last_read_hybrid_time`

Where `hybrid_clock_time` is the hybrid clock timestamp on the producer's tablet-server, and `last_read_hybrid_time` is the hybrid clock timestamp of the latest transaction pulled from the producer.

An example script [`determine_replication_lag.sh`](/files/determine_replication_lag.sh) calculates replication lag for you. The script requires the [`jq`](https://stedolan.github.io/jq/) package.

The following example generates a replication lag summary for all tables on a cluster. You can also request an individual table.

```sh
./determine_repl_latency.sh -m 10.150.255.114,10.150.255.115,10.150.255.113
```

Call `determine_repl_latency.sh -h` to get a summary of all command options.

{{< /tip >}}

**If you're using Yugabyte Platform to manage universes**, continue to the next section. If not, you're done!

## Enable universes in Platform

If you're using Yugabyte Platform to manage your universes, you need to call the following REST API endpoint on your Platform instance _for each universe_ (producer and consumer) involved in the 2DC replication:

**PUT** /api/customers/<_myUUID_>/universes/<_myUniUUID_>/setup_universe_2dc

Where `myUUID` is your customer UUID, and `myUniUUID` is the UUID of the universe (producer or consumer). The request should include an `X-AUTH-YW-API-TOKEN` header with your Platform API key.

Here is a sample `curl` command:

```sh
curl -X PUT \
  -H "X-AUTH-YW-API-TOKEN: myPlatformApiToken" \
  https://myPlatformServer/api/customers/myUUID/universes/myUniUUID/setup_universe_2dc
```

**To find your customer UUID** in Yugabyte Platform:

1. Click the person icon at the top left of any Platform page, and navigate to Profile.

1. On the General tab, copy your API token. If the API Token field is blank, click Generate Key, then copy the resulting API token.

    {{< note >}}

Generating a new API token invalidates your existing token. Only the most-recently generated API token is valid.

    {{< /note >}}

1. From a command line, issue a `curl` command of the following form:

    ```sh
    curl \
      -H "X-AUTH-YW-API-TOKEN: myPlatformApiToken" \
      [http|https]://myPlatformServer/api/customers
    ```

    For example:

    ```sh
    curl -X "X-AUTH-YW-API-TOKEN: e5c6eb2f-7e30-4d5e-b0a2-f197b01d9f79" \
      http://localhost/api/customers
    ```

1. Copy your UUID from the resulting JSON output, without the double quotes and square brackets:

    ```
    ["6553ea6d-485c-4ae8-861a-736c2c29ec46"]
    ```

**To find a universe's UUID** in Yugabyte Platform, click Universes in the left column, then click the name of the Universe. The URL of the universe's Overview page ends with the universe's UUID. (For example, `http://myPlatformServer/universes/d73833fc-0812-4a01-98f8-f4f24db76dbe`)
