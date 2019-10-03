---
title: Replicate between 2 data centers
linkTitle: Replicate between 2 data centers
description: Replicate between two data centers
beta: /faq/product/#what-is-the-definition-of-the-beta-feature-tag
menu:
  latest:
    parent: deploy
    identifier: replicate-2dc
    weight: 633
type: page
isTocNested: true
showAsideToc: true
---

Follow the steps below to set up a two data center (2DC) deployment using either unidirectional (one-way) or bidirectional (two-way) replication between the data centers.

For more information on the two data center (2DC) deployment architecture and supported replication scenarios, see [Two data center (2DC) deployments](../../architecture/2dc-deployments).

## Set up a two data center replication

### Set up Universe 1 (producer universe)

To create the producer universe, follow these steps.

1. Create the “yugabyte-producer” universe.

2. Create the the tables for the APIs being used.

### Set up Universe 2 (consumer universe)

To create the consumer universe, follow the steps below.

1. Create the “yugabyte-consumer” universe.

2. Create the tables for the APIs you are using.

Make sure to create the same tables as you did for the producer universe.

After creating the required tables, you can now set up the replication behavior.

## Set up replication for the two data centers

## Unidirectional (one-way) replication

1. Look up the producer universe UUID and the table IDs for the two tables and the index table on master UI.

2. Run the following `yb-admin` command.

```sh
yb-admin -master_addresses <consumer_universe_master_addresses>
setup_universe_replication <producer_universe_uuid>
  <producer_universe_master_addresses>
  <table_id>,[<table_id>..]
```

### Example

```sh
yb-admin -master_addresses 127.0.0.11:7100,127.0.0.12:7100,127.0.0.13:7100 setup_universe_replication e260b8b6-e89f-4505-bb8e-b31f74aa29f3 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 000030a5000030008000000000004000,000030a5000030008000000000004005,dfef757c415c4b2cacc9315b8acb539a
```

{{< note title="Note" >}}

There should be three table IDs in the command above — two of those are YSQL for base table and index, and one for CQL table. Make sure to specify all master addresses for both producer and consumer universes in the command.

{{< /note >}}

## Bidirectional (two-way) replication

To set up 2-way replication, follow the steps above in [Set up one-way (unidirectional) replication] and then do the same steps for the the “yugabyte-producer” universe.

Note that this time, “yugabyte-producer” will be set up to consume data from “yugabyte-consumer”.

### Load data into producer universe

1. Start pumping data into “yugabyte-producer” using `yb-sample-apps`.

#### YSQL example

```sh
java -jar target/yb-sample-apps.jar --workload SqlSecondaryIndex  --nodes 127.0.0.1:5433
```

#### YCQL example

```sh
java -jar target/yb-sample-apps.jar --workload CassandraBatchKeyValue --nodes 127.0.0.1:9042
```

### Verify replication

**For one-way replication:**

Connect to “yugabyte-consumer” universe using the YSQL shell (`ysqlsh`) or the YCQL shell (`cqlsh`), and then confirm that you can see expected records.

**For two-way replication:**

Repeat steps above, but pump data into “yugabyte-consumer”. To avoid primary key conflict errors, keep the key space for the two universes separate.
