---
title: 2DC replication
linkTitle: 2DC replication
description: Replicating two data centers
menu:
  latest:
    parent: change-data-capture
    identifier: 2dc-replication
    weight: 653
type: page
isTocNested: true
showAsideToc: true
---



## Set up a 2DC replication

### Set up Universe 1 (producer universe)

To create the producer universe, follow these steps.

1. Create Universe “yugabyte-producer”

2. Create YSQL table with index

   ```sql
   CREATE TABLE sqlsecondaryindex(
     k text PRIMARY KEY,
     v text);
   CREATE INDEX sql_idx ON sqlsecondaryindex(v);
   ```

3. Create YCQL table

```sql
CREATE KEYSPACE ybdemo_keyspace;
CREATE TABLE ybdemo_keyspace.cassandrakeyvalue (k varchar, v blob, primary key (k));
```

### Set up Universe 2 (consumer universe)

To create the consumer universe, follow the steps below.

1. Create Universe “yugabyte-consumer”

2. Create the same YSQL table and index

```sql
CREATE TABLE sqlsecondaryindex(
  k text PRIMARY KEY,
  v text);
CREATE INDEX sql_idx ON sqlsecondaryindex(v);
```

3. Create the same YCQL table.

```sql
CREATE KEYSPACE ybdemo_keyspace;
CREATE TABLE ybdemo_keyspace.cassandrakeyvalue (
  k varchar, 
  v blob, 
  primary key (k));
```

## Set up one-way (unidirectional) replication

1. Set up one-way replication on those two tables.

2. Look up the producer universe UUID and the table IDs for the two tables and the index table on master UI.

3. Run this `yb-admin` command

```bash
yb-admin -master_addresses <consumer_universe_master_addresses> 
setup_universe_replication <producer_universe_uuid> 
  <producer_universe_master_addresses> 
  <table_id>,[<table_id>..]
```

### Example

```bash
yb-admin -master_addresses 127.0.0.11:7100,127.0.0.12:7100,127.0.0.13:7100 setup_universe_replication e260b8b6-e89f-4505-bb8e-b31f74aa29f3 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 000030a5000030008000000000004000,000030a5000030008000000000004005,dfef757c415c4b2cacc9315b8acb539a
```

{{< note title="Note" >}}

There should be three table IDs in the command above — two of those are YSQL for base table and index, and one for CQL table. Make sure to specify all master addresses for both producer and consumer universes in the command.

{{< /note >}}

## Set up two-way (bidirectional) replication

If you want to set up 2-way replication, then repeat the above command on “yugabyte-producer” universe. 

Note that this time, “yugabyte-producer” will be set up to consume data from “yugabyte-consumer”.

### Load data into producer universe

1. Start pumping data into “yugabyte-producer” using `yb-sample-apps`.

#### YSQL Example

```bash
java -jar target/yb-sample-apps.jar --workload SqlSecondaryIndex  --nodes 127.0.0.1:5433

#### YCQL Example

```bash
java -jar target/yb-sample-apps.jar --workload CassandraBatchKeyValue --nodes 127.0.0.1:9042
```

### Verify replication

1. Connect to “yugabyte-consumer” universe using the YSQL shell (`ysqlsh`) or the YCQL shell (`cqlsh`), and then confirm that you can see expected records.

### Verify 2-way replication

For 2-way replication, repeat steps (10) and (11), but pump data into “yugabyte-consumer”. To avoid primary key conflict errors, keep the key space for the two universes separate.
