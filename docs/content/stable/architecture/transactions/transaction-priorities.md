---
title: Transaction priorities in YugabyteDB YSQL
headerTitle: Transaction priorities
linkTitle: Transaction priorities
description: Details about Transaction priorities in YSQL
menu:
  stable:
    identifier: architecture-transaction-priorities
    parent: architecture-acid-transactions
    weight: 1153
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../transaction-priorities/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

</ul>

When using the [Fail-on-Conflict](../concurrency-control/#fail-on-conflict) concurrency control policy, transactions are assigned priorities that help decide which transactions should be aborted in case of conflict.

There are two priority buckets, each having a priority range of [reals](https://www.postgresql.org/docs/current/datatype.html) in [0, 1] as follows:

1. `High-priority` bucket: if the first statement in a transaction takes a `FOR UPDATE/ FOR SHARE/ FOR NO KEY UPDATE` explicit row lock using SELECT, it will be assigned a priority from this bucket.

2. `Normal-priority` bucket: all other transactions are assigned a priority from this bucket.

Note that a transaction with any priority P1 from the high-priority bucket can abort a transaction with any priority P2 from the normal-priority bucket. For example, a transaction with priority 0.1 from the high-priority bucket can abort a transaction with priority 0.9 from the normal-priority bucket.

Priorities are randomly chosen from the applicable bucket. However, you can use the following two YSQL parameters to control the priority assigned to transactions in a specific session:

- `yb_transaction_priority_lower_bound`
- `yb_transaction_priority_upper_bound`

These parameters help set lower and upper bounds on the randomly-assigned priority that a transaction should receive from the applicable bucket. These parameters accept a value of `real` datatype in the range [0, 1]. Also note that the same bounds apply to both buckets.

{{< note title="All single shard transactions have a priority of 1 in the normal-priority bucket." >}}
{{</note >}}

The `yb_get_current_transaction_priority` function can be used to fetch the transaction priority of the current active transaction. It outputs a pair `<priority> (bucket)`, where `<priority>` is of a real datatype between [0, 1] with 9 decimal units of precision, and `<bucket>` is either `Normal` or `High`.

{{< note title="Note">}}
As an exception, if a transaction is assigned the highest priority possible, that is, a priority of 1 in the high-priority bucket, the function returns `highest priority transaction` without any real value.
{{</note >}}

A transaction's priority is `0.000000000 (normal-priority transaction)` until a transaction is really started.

## Examples

The following examples demonstrate how to set priorities for your transactions and get the current transaction priority.

1. Create a table and insert some data.

    ```sql
    CREATE TABLE test (k INT PRIMARY KEY, v INT);
    INSERT INTO test VALUES (1, 1);
    ```

1. Set the lower and upper bound values for your transactions as follows:

    ```sql
    SET yb_transaction_priority_lower_bound = 0.4;
    SET yb_transaction_priority_upper_bound = 0.6;
    ```

1. Create a transaction in the normal-priority bucket as follows:

    ```sql
    BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
    SELECT yb_get_current_transaction_priority(); -- 0 due to an optimization which doesn't really start a real transaction internally unless a write occurs
    ```

    ```output
        yb_get_current_transaction_priority
    -------------------------------------------
     0.000000000 (Normal priority transaction)
    (1 row)
    ```

    ```sql
    SELECT * FROM test;
    ```

    ```output
     k | v
    ---+---
     1 | 1
    (1 row)
    ```

    ```sql
    SELECT yb_get_current_transaction_priority(); -- still 0 due to the optimization which doesn't really start a real transaction internally unless a write occurs
    ```

    ```output
        yb_get_current_transaction_priority
    -------------------------------------------
     0.000000000 (Normal priority transaction)
    (1 row)
    ```

    ```sql
    INSERT INTO test VALUES (2, '2'); -- perform a write which starts a real     transaction
    SELECT yb_get_current_transaction_priority(); -- non-zero now
    ```

    ```output
        yb_get_current_transaction_priority
    -------------------------------------------
     0.537144608 (Normal priority transaction)
    (1 row)
    ```

    ```sql
    COMMIT;
    ```

1. Create a transaction in the high-priority bucket as follows:

    ```sql
    BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
    SELECT * FROM test WHERE k = 1 FOR UPDATE; -- starts a transaction in a high-priority bucket
    ```

    ```output
     k | v
    ---+---
     1 | 1
    (1 row)
    ```

    ```sql
    SELECT yb_get_current_transaction_priority();
    ```

    ```output
       yb_get_current_transaction_priority
    -----------------------------------------
     0.412004009 (High priority transaction)
    (1 row)
    ```

    ```sql
    COMMIT;
    ```

1. Create a transaction with the highest priority

    ```sql
    SET yb_transaction_priority_upper_bound = 1;
    SET yb_transaction_priority_lower_bound = 1;
    BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
    SELECT * FROM test WHERE k = 1 FOR UPDATE;
    ```

    ```output
     k | v
    ---+---
     1 | 1
    (1 row)
    ```

    ```sql
    SELECT yb_get_current_transaction_priority();
    ```

    ```output
     yb_get_current_transaction_priority
    -------------------------------------
     Highest priority transaction
    (1 row)
    ```

    ```sql
    COMMIT;
    ```

{{< note title="Internal representation of priorities" >}}

Internally, both the normal and high-priority buckets are mapped to a `uint64_t` space. The 64 bit range is used by the two priority buckets as follows:

1. Normal-priority bucket: `[yb::kRegularTxnLowerBound, yb::kRegularTxnUpperBound]`, that is, 0 to  `uint32_t_max`-1

1. High-priority bucket: `[yb::kHighPriTxnLowerBound, yb::kHighPriTxnUpperBound]`, that is, `uint32_t_max` to `uint64_t_max`

For ease of use, the bounds are expressed as a [0, 1] real range for each bucket in the lower or upper bound YSQL parameters and the `yb_get_current_transaction_priority` function. The [0, 1] real range map proportionally to the integer ranges for both buckets. In other words, the [0, 1] range in the normal-priority bucket maps to `[0, uint32_t_max-1]` and the [0, 1] range in the high-priority bucket maps to `[uint32_t_max, uint64_t_max]`.

{{< /note >}}
