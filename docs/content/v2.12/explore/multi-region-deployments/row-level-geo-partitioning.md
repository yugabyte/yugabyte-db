---
title: Row-level geo-partitioning
linkTitle: Row-level geo-partitioning
description: Row-level geo-partitioning in YSQL
headcontent: Pin data to regions for compliance and lower latencies
image: /images/section_icons/secure/create-roles.png
menu:
  v2.12:
    identifier: explore-multi-region-geo-partitioning
    parent: explore-multi-region-deployments
    weight: 720
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../row-level-geo-partitioning/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
</ul>

Row-level geo-partitioning allows fine-grained control over pinning data in a user table (at a per-row level) to geographic locations, thereby allowing the data residency to be managed at the table-row level. Use-cases requiring low latency multi-region deployments, transactional consistency semantics and transparent schema change propagation across the regions would benefit from this feature.

Geo-partitioning allows you to move data closer to users to:

* achieve lower latency and higher performance
* meet data residency requirements to comply with regulations such as GDPR

Geo-partitioning of data enables fine-grained, row-level control over the placement of table data across different geographical locations. This is accomplished in two steps:

1. Partition a table into user-defined table partitions.
1. Pin these partitions to the desired geographic locations by configuring metadata for each partition.

To create user-defined table partitions, designate a column of the table as the partition column that will be used to geo-partition the data. The value of this column for a given row is used to determine the table partition that the row belongs to.

The second step involves creating partitions in the respective geographic locations using tablespaces. Note that the data in each partition can be configured to get replicated across multiple zones in a cloud provider region, or across multiple nearby regions or data centers.

An entirely new geographic partition can be introduced dynamically by adding a new table partition and configuring it to keep the data resident in the desired geographic location. Data in one or more of the existing geographic locations can be purged efficiently by dropping the necessary partitions. Users of traditional RDBMS would recognize this scheme as being close to user-defined list-based table partitions, with the ability to control the geographic location of each partition.

In this deployment, users can access their data with low latencies because the data resides on servers that are geographically close by, and the queries do not need to access data in far away geographic locations.

This tutorial explains this feature in the context of an example scenario described in the next section.

## Example scenario

Suppose a large but imaginary bank, Yuga Bank, wants to offer an online banking service to users in many countries by processing their deposits, withdrawals, and transfers.

The following attributes would be required to build such a service:

* **Transactional semantics with high availability:** Consistency of data is paramount in a banking application, hence the database should be ACID compliant. Additionally, users expect the service to always be available, making high availability and resilience to failures a critical requirement.
* **High performance:** The online transactions need to be processed with a low latency in order to ensure a good end-user experience. This requires that the data for a particular user is located in a nearby geographic region. Putting all the data in a single location in an RDBMS would mean the requests for users residing far away from that location would have very high latencies, leading to a poor user experience.
* **Data residency requirements for compliance:** Many countries have regulations around which geographic regions the personal data of their residents can be stored in, and bank transactions being personal data are subject to these requirements. For example, GDPR has a _data residency_ stipulation which effectively requires that the personal data of individuals in the EU be stored in the EU. Similarly, India has a requirement issued by the Reserve Bank of India (or RBI for short) making it mandatory for all banks, intermediaries, and other third parties to store all information pertaining to payments data in India – though in case of international transactions, the data on the foreign leg of the transaction can be stored in foreign locations.

{{< note title="Note" >}}
While this scenario has regulatory compliance requirements where data needs to be resident in certain geographies, the exact same technique applies for the goal of moving data closer to users to achieve low latency and high performance. Hence, high performance is listed above as a requirement.
{{< /note >}}

## Step 1. Create tablespaces

First, create tablespaces and transaction tables for each geographic region you wish to partition data into:

1. Create tablespaces for each region.

    ```sql
    CREATE TABLESPACE eu_central_1_tablespace WITH (
      replica_placement='{"num_replicas": 3, "placement_blocks":
      [{"cloud":"aws","region":"eu-central-1","zone":"eu-central-1a","min_num_replicas":1},
      {"cloud":"aws","region":"eu-central-1","zone":"eu-central-1b","min_num_replicas":1},
      {"cloud":"aws","region":"eu-central-1","zone":"eu-central-1c","min_num_replicas":1}]}'
    );

    CREATE TABLESPACE us_west_2_tablespace WITH (
      replica_placement='{"num_replicas": 3, "placement_blocks":
      [{"cloud":"aws","region":"us-west-2","zone":"us-west-2a","min_num_replicas":1},
      {"cloud":"aws","region":"us-west-2","zone":"us-west-2b","min_num_replicas":1},
      {"cloud":"aws","region":"us-west-2","zone":"us-west-2c","min_num_replicas":1}]}'
    );

    CREATE TABLESPACE ap_south_1_tablespace WITH (
      replica_placement='{"num_replicas": 3, "placement_blocks":
      [{"cloud":"aws","region":"ap-south-1","zone":"ap-south-1a","min_num_replicas":1},
      {"cloud":"aws","region":"ap-south-1","zone":"ap-south-1b","min_num_replicas":1},
      {"cloud":"aws","region":"ap-south-1","zone":"ap-south-1c","min_num_replicas":1}]}'
    );
    ```

1. Create transaction tables for use in each region. (Replace the IP addresses with those of your YB-Master servers.)

    ```sh
    ./bin/yb-admin \
        -master_addresses 127.0.0.1:7100,127.0.0.4:7100,127.0.0.7:7100 \
        create_transaction_table transactions_eu_central_1
    ```

    ```sh
    ./bin/yb-admin \
        -master_addresses 127.0.0.1:7100,127.0.0.4:7100,127.0.0.7:7100 \
        create_transaction_table transactions_us_west_2
    ```

    ```sh
    ./bin/yb-admin \
        -master_addresses 127.0.0.1:7100,127.0.0.4:7100,127.0.0.7:7100 \
        create_transaction_table transactions_ap_south_1
    ```

1. Assign placement for each transaction table. (Replace the IP addresses with those of your YB-Master servers.)

    ```sh
    ./bin/yb-admin \
        -master_addresses 127.0.0.1:7100,127.0.0.4:7100,127.0.0.7:7100 \
        modify_table_placement_info system transactions_eu_central_1 \
        aws.eu-central-1.eu-central-1a,aws.eu-central-1.eu-central-1b,aws.eu-central-1.eu-central-1c 3
    ```

    ```sh
    ./bin/yb-admin \
        -master_addresses 127.0.0.1:7100,127.0.0.4:7100,127.0.0.7:7100 \
        modify_table_placement_info system transactions_us_west_2 \
        aws.us-west-2.us-west-2a,aws.us-west-2.us-west-2b,aws.us-west-2.us-west-2c 3
    ```

    ```sh
    ./bin/yb-admin \
        -master_addresses 127.0.0.1:7100,127.0.0.4:7100,127.0.07:7100 \
        modify_table_placement_info system transactions_ap_south_1 \
        aws.ap-south-1.ap-south-1a,aws.ap-south-1.ap-south-1b,aws.ap-south-1.ap-south-1c 3
    ```

## Step 2. Create table with partitions

Next, create the parent table that contains a `geo_partition` column which is used to create list-based partitions for each geographic region you want to partition data into as shown in the following diagram:

![Row-level geo-partitioning](/images/explore/multi-region-deployments/geo-partitioning-1.png)

1. Create the parent table.

    ```sql
    CREATE TABLE bank_transactions (
        user_id   INTEGER NOT NULL,
        account_id INTEGER NOT NULL,
        geo_partition VARCHAR,
        account_type VARCHAR NOT NULL,
        amount NUMERIC NOT NULL,
        txn_type VARCHAR NOT NULL,
        created_at TIMESTAMP DEFAULT NOW()
    ) PARTITION BY LIST (geo_partition);
    ```

1. Next, create one partition per desired geography under the parent table, and assign each to the applicable tablespace. Here, you create three table partitions: one for the EU region called `bank_transactions_eu`, another for the India region called `bank_transactions_india,` and a third partition for US region called `bank_transactions_us`. Create any required indexes for each partition, making sure to associate each index with the same tablespace as that of the partition table.

    ```sql
    CREATE TABLE bank_transactions_eu
        PARTITION OF bank_transactions
          (user_id, account_id, geo_partition, account_type,
          amount, txn_type, created_at,
          PRIMARY KEY (user_id HASH, account_id, geo_partition))
        FOR VALUES IN ('EU') TABLESPACE eu_central_1_tablespace;

    CREATE INDEX ON bank_transactions_eu(account_id) TABLESPACE eu_central_1_tablespace;

    CREATE TABLE bank_transactions_india
        PARTITION OF bank_transactions
          (user_id, account_id, geo_partition, account_type,
          amount, txn_type, created_at,
          PRIMARY KEY (user_id HASH, account_id, geo_partition))
        FOR VALUES IN ('India') TABLESPACE ap_south_1_tablespace;

    CREATE INDEX ON bank_transactions_india(account_id) TABLESPACE ap_south_1_tablespace;

    CREATE TABLE bank_transactions_us
        PARTITION OF bank_transactions
          (user_id, account_id, geo_partition, account_type,
          amount, txn_type, created_at,
          PRIMARY KEY (user_id HASH, account_id, geo_partition))
        FOR VALUES IN ('US') TABLESPACE us_west_2_tablespace;

    CREATE INDEX ON bank_transactions_us(account_id) TABLESPACE us_west_2_tablespace;
    ```

1. Use the `\d` meta-command to view the table and partitions you've created so far.

    ```sql
    yugabyte=# \d
    ```

    ```output
                    List of relations
     Schema |         Name                      | Type  |  Owner
    --------+-----------------------------------+-------+----------
     public | bank_transactions         | table | yugabyte
     public | bank_transactions_eu      | table | yugabyte
     public | bank_transactions_india   | table | yugabyte
     public | bank_transactions_us      | table | yugabyte
    (4 rows)
    ```

The data is now arranged as follows:

![Row-level geo-partitioning](/images/explore/multi-region-deployments/geo-partitioning-2.png)

## Step 3. Pin user partitions specific to geographic locations

Now, the setup should automatically be able to pin rows to the appropriate regions based on the  value set in the `geo_partition` column. This is shown in the following diagram:

![Row-level geo-partitioning](/images/explore/multi-region-deployments/geo-partitioning-3.png)

You can test this region pinning by inserting a few rows of data and verifying they are written to the correct partitions.

{{< tip title="Expanded output display" >}}
The sample output includes expanded auto mode output formatting for better readability. You can enable this mode with the following statement:

```sql
yugabyte=# \x auto
```

```output
Expanded display is used automatically.
```

{{</ tip >}}

1. Insert a row into the table with the `geo_partition` column value set to `EU` below.

    ```sql
    INSERT INTO bank_transactions
        VALUES (100, 10001, 'EU', 'checking', 120.50, 'debit');
    ```

1. Verify that the row is present in the `bank_transactions` table.

    ```sql
    yugabyte=# select * from bank_transactions;
    ```

    ```output
    -[ RECORD 1 ]-+---------------------------
    user_id       | 100
    account_id    | 10001
    geo_partition | EU
    account_type  | checking
    amount        | 120.5
    txn_type      | debit
    created_at    | 2020-11-07 21:28:11.056236
    ```

Additionally, the row must be present only in the `bank_transactions_eu` partition, which can be easily verified by running the select statement directly against that partition. The other partitions should contain no rows.

```sql
yugabyte=# select * from bank_transactions_eu;
```

```output
-[ RECORD 1 ]-+---------------------------
user_id       | 100
account_id    | 10001
geo_partition | EU
account_type  | checking
amount        | 120.5
txn_type      | debit
created_at    | 2020-11-07 21:28:11.056236
```

```sql
yugabyte=# select count(*) from bank_transactions_india;
```

```output
 count
-------
     0
```

```sql
yugabyte=# select count(*) from bank_transactions_us;
```

```sql
 count
-------
     0
```

Insert data into the other partitions.

```sql
INSERT INTO bank_transactions
    VALUES (200, 20001, 'India', 'savings', 1000, 'credit');
INSERT INTO bank_transactions
    VALUES (300, 30001, 'US', 'checking', 105.25, 'debit');
```

These can be verified as follows:

```sql
yugabyte=# select * from bank_transactions_india;
```

```output
-[ RECORD 1 ]-+---------------------------
user_id       | 200
account_id    | 20001
geo_partition | India
account_type  | savings
amount        | 1000
txn_type      | credit
created_at    | 2020-11-07 21:45:26.011636
```

```sql
yugabyte=# select * from bank_transactions_us;
```

```output
-[ RECORD 1 ]-+---------------------------
user_id       | 300
account_id    | 30001
geo_partition | US
account_type  | checking
amount        | 105.25
txn_type      | debit
created_at    | 2020-11-07 21:45:26.067444
```

## Step 4. Query the local partition

Querying from a particular partition can be accomplished by using a `WHERE` clause on the partition key. For example, if the client is in the US, querying the local partition can be done by running the following query:

```sql
yugabyte=# select * from bank_transactions where geo_partition='US';
```

```output
-[ RECORD 1 ]-+---------------------------
user_id       | 300
account_id    | 30001
geo_partition | US
account_type  | checking
amount        | 105.25
txn_type      | debit
created_at    | 2020-11-07 21:45:26.067444
```

However, if you need to query the local partition without specifying the partition column, you can use the function [yb_is_local_table](../../../api/ysql/exprs/func_yb_is_local_table). To implement the same query as above using `yb_is_local_table`, you can do the following:

```sql
yugabyte=# select * from bank_transactions where yb_is_local_table(tableoid);
```

```output
-[ RECORD 1 ]-+---------------------------
user_id       | 300
account_id    | 30001
geo_partition | US
account_type  | checking
amount        | 105.25
txn_type      | debit
created_at    | 2020-11-07 21:45:26.067444
```

## Step 5. Users travelling across geographic locations

To make things interesting, say user 100, whose first bank transaction was performed in the EU region, travels to India and the US, and performs two other bank transactions. This can be simulated by using the following statements:

```sql
INSERT INTO bank_transactions
    VALUES (100, 10001, 'India', 'savings', 2000, 'credit');
INSERT INTO bank_transactions
    VALUES (100, 10001, 'US', 'checking', 105, 'debit');
```

Now, each of the bank transactions would be pinned to the appropriate geographic locations. This can be verified as follows.

```sql
yugabyte=# select * from bank_transactions_india where user_id=100;
```

```output
-[ RECORD 1 ]-+---------------------------
user_id       | 100
account_id    | 10001
geo_partition | India
account_type  | savings
amount        | 2000
txn_type      | credit
created_at    | 2020-11-07 21:56:26.760253
```

```sql
yugabyte=# select * from bank_transactions_us where user_id=100;
```

```output
-[ RECORD 1 ]-+---------------------------
user_id       | 100
account_id    | 10001
geo_partition | US
account_type  | checking
amount        | 105
txn_type      | debit
created_at    | 2020-11-07 21:56:26.794173
```

All the bank transactions made by the user can be retrieved using the following SQL statement:

```sql
yugabyte=# select * from bank_transactions where user_id=100 order by created_at desc;
```

```output
-[ RECORD 1 ]-+---------------------------
user_id       | 100
account_id    | 10001
geo_partition | US
account_type  | checking
amount        | 105
txn_type      | debit
created_at    | 2020-11-07 21:56:26.794173
-[ RECORD 2 ]-+---------------------------
user_id       | 100
account_id    | 10001
geo_partition | India
account_type  | savings
amount        | 2000
txn_type      | credit
created_at    | 2020-11-07 21:56:26.760253
-[ RECORD 3 ]-+---------------------------
user_id       | 100
account_id    | 10001
geo_partition | EU
account_type  | checking
amount        | 120.5
txn_type      | debit
created_at    | 2020-11-07 21:28:11.056236
```

## Step 6. Run distributed transactions

So far, you have only been running `SELECT` and [single-row transactions](../../../architecture/transactions/transactions-overview/#single-row-transactions). Geo-partitioning introduces a new complication for general distributed transactions.

Suppose you want to run the following transaction:

```sql
BEGIN;
INSERT INTO bank_transactions VALUES (100, 10002, 'EU', 'checking', 400.00, 'debit');
INSERT INTO bank_transactions VALUES (100, 10003, 'EU', 'checking', 400.00, 'credit');
COMMIT;
```

An attempt to run this while connected to a node in `us-west-2` will result in an error:

```sql
BEGIN;
INSERT INTO bank_transactions VALUES (100, 10002, 'EU', 'checking', 400.00, 'debit');
```

```output
ERROR:  Illegal state: Nonlocal tablet accessed in local transaction: tablet c5a611afd571455e80450bd553a24a64: . Errors from tablet servers: [Illegal state (yb/client/transaction.cc:284): Nonlocal tablet accessed in local transaction: tablet c5a611afd571455e80450bd553a24a64]
```

Because you've created a transaction table for `us-west-2`, YugabyteDB assumes by default that you want to run a transaction local to that region (using the transaction status table `system.transactions_us_west_2` you created in Step 1), but such a transaction cannot modify data outside of us-west-2.

However, if you connect to a node in `eu-central-1` and run the exact same transaction, you will be able to finish and commit the transaction without an error.

```sql
BEGIN;
INSERT INTO bank_transactions VALUES (100, 10002, 'EU', 'checking', 400.0, 'debit');
```

```output
INSERT 1 0
```

```sql
INSERT INTO bank_transactions VALUES (100, 10003, 'EU', 'checking', 400.0, 'credit');
```

```output
INSERT 1 0
```

```sql
COMMIT;
```

```output
COMMIT
```

Sometimes though, you might want to run a transaction that writes data to multiple regions, for example:

```sql
BEGIN;
INSERT INTO bank_transactions VALUES (100, 10004, 'US', 'checking', 400.00, 'debit');
INSERT INTO bank_transactions VALUES (200, 10005, 'EU', 'checking', 400.00, 'credit');
COMMIT;
```

Running this transaction will fail whether you run it from `us-west-2` or `eu-central-1`. The solution is to mark the transaction as a global transaction:

```sql
SET force_global_transaction = TRUE;
BEGIN;
INSERT INTO bank_transactions VALUES (100, 10004, 'US', 'checking', 400.00, 'debit');
```

```output
INSERT 1 0
```

```sql
INSERT INTO bank_transactions VALUES (200, 10005, 'EU', 'checking', 400.00, 'credit');
```

```output
INSERT 1 0
```

```sql
COMMIT;
```

```output
COMMIT
```

Setting `force_global_transaction = TRUE` tells YugabyteDB to use the `system.transactions` transaction table instead, which is presumed to be globally replicated, and lets you run distributed transactions that span multiple regions.

{{< note title="Global transaction latency" >}}
Only force global transactions when necessary. All distributed transactions _can_ run without problems under `force_global_transaction = TRUE`, but you may have significantly higher latency when committing the transaction, because YugabyteDB must achieve consensus across multiple regions to write to `system.transactions`. Whenever possible, use the default setting of `force_global_transaction = FALSE`.
{{< /note >}}

Finally, suppose you want to delete the row you just inserted. If you run the following query connected to `eu-central-1` as a local transaction, the query once again errors out:

```sql
SET force_global_transaction = FALSE;
DELETE FROM bank_transactions WHERE user_id = 200 AND account_id = 10005;
```

```output
ERROR:  Illegal state: Nonlocal tablet accessed in local transaction: tablet c5a611afd571455e80450bd553a24a64: . Errors from tablet servers: [Illegal state (yb/client/transaction.cc:284): Nonlocal tablet accessed in local transaction: tablet c5a611afd571455e80450bd553a24a64]
```

You are attempting to delete from the main table (`bank_transactions` rather than `bank_transactions_eu_west_1`) and not specifying the partition column (there's no `geo_partition = 'EU'` clause). This means that YugabyteDB is unable to tell that the row being deleted is in fact located in eu-central-1. To fix this, run the following command:

```sql
DELETE FROM bank_transactions_eu_west_1 WHERE user_id = 200 AND account_id = 10005;
```

```output
DELETE 1
```

## Step 7. Add a new geographic location

Assume that after a while, Yuga Bank gets a lot of customers across the globe, and wants to offer the service to residents of Brazil, which also has data residency laws. Using row-level geo-partitioning, you can do this by adding a new partition and pinning it to the AWS South America (São Paulo) region `sa-east-1`.

First, create the tablespace:

```sql
CREATE TABLESPACE sa_east_1_tablespace WITH (
    replica_placement='{"num_replicas": 3, "placement_blocks":
      [{"cloud":"aws","region":"sa-east-1","zone":"sa-east-1a","min_num_replicas":1},
      {"cloud":"aws","region":"sa-east-1","zone":"sa-east-1b","min_num_replicas":1},
      {"cloud":"aws","region":"sa-east-1","zone":"sa-east-1c","min_num_replicas":1}]}'
    );
```

Next, create the transaction table and adjust the placement. (Replace the IP addresses with those of your YB-Master servers.)

```sh
./bin/yb-admin \
    -master_addresses 127.0.0.1:7100,127.0.0.4:7100,127.0.0.7:7100 \
    create_transaction_table transactions_sa_east_1
```

```sh
./bin/yb-admin \
    -master_addresses 127.0.0.1:7100,127.0.0.4:7100,127.0.0.7:7100 \
    modify_table_placement_info system transactions_sa_east_1 \
    aws.sa-east-1.sa-east-1a,aws.sa-east-1.sa-east-1b,aws.sa-east-1.sa-east-1c 3
```

Finally, create the partition for Brazil:

```sql
CREATE TABLE bank_transactions_brazil
    PARTITION OF bank_transactions
      (user_id, account_id, geo_partition, account_type,
       amount, txn_type, created_at,
       PRIMARY KEY (user_id HASH, account_id, geo_partition))
    FOR VALUES IN ('Brazil') TABLESPACE sa_east_1_tablespace;
```

And with that, the new region is ready to store bank transactions of the residents of Brazil.

```sql
INSERT INTO bank_transactions
    VALUES (400, 40001, 'Brazil', 'savings', 1000, 'credit');

select * from bank_transactions_brazil;
```

```output
-[ RECORD 1 ]-+-------------------------
user_id       | 400
account_id    | 40001
geo_partition | Brazil
account_type  | savings
amount        | 1000
txn_type      | credit
created_at    | 2020-11-07 22:09:04.8537
```
