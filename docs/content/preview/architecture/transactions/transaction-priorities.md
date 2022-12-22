---
title: Transaction priorities
headerTitle: Transaction priorities
linkTitle: Transaction priorities
description: Details about Transaction priorities in YSQL
menu:
  preview:
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

When using the [Fail-on-Conflict](../concurrency-control/#fail-on-conflict) concurrency control policy, transactions are assigned priorities that help decide which transactions should be aborted on conflicts.

For distributed transactions, the priority space is `unit64_t`. The 64 bit range is split into 2 priority buckets -

1. Normal-priority bucket: `[yb::kRegularTxnLowerBound, yb::kRegularTxnUpperBound]` i.e., 0 to  `uint32_t_max`-1
2. High-priority bucket: `[yb::kHighPriTxnLowerBound, yb::kHighPriTxnUpperBound]` i.e., `uint32_t_max` to `uint64_t_max`

All transactions are usually randomly assigned a priority in the first bucket (normal). However, in case the first statement in a transaction takes a `FOR UPDATE/ FOR SHARE/ FOR NO KEY UPDATE` explicit row lock using SELECT, it will be assigned a priority from the high-priority bucket.

Apart from the above rule, there are two other user configurable session variables that can help control the priority assigned to transaction is a specific session. These are `yb_transaction_priority_lower_bound` and `yb_transaction_priority_upper_bound`. These help set lower and upper bounds on the randomly assigned priority a transaction should receive from the respective bucket that applies to it. For ease of use, the bounds are expressed as a float in [0, 1] specifying which range of the applicable bucket we should use to assign priorities. Also note that the same floating point bounds apply to both buckets.

For example, if `yb_transaction_priority_lower_bound=0.5` and `yb_transaction_priority_upper_bound=0.75`:

1. a transaction that is assigned a priority from the normal bucket will get one between the 0.5-0.75 marks such that the `[yb::kRegularTxnLowerBound, yb::kRegularTxnUpperBound]` range proportionally maps to 0-1.

2. a transaction that is assigned a priority from the high-priority bucket will get between the 0.5-0.75 marks such that the `[yb::kHighPriTxnLowerBound, yb::kHighPriTxnUpperBound]` range proportionally maps to 0-1.

{{< note title="All single shard transactions have a priority of kHighPriTxnLowerBound-1" >}}
{{</note >}}

The `yb_get_current_transaction_priority` function can be used to fetch the transaction prriority of the current active transaction. It returns of a pair of two values -

1. A float between 0-1 inclusive with 9 decimal units of precision that such that it proportionally maps to the priority assigned in the range of the priority bucket the transaction belongs in.

2. The bucket in which the transaction's priority lies - `Normal` or `High` priority.

NOTE: As an exception, if a transaction is assigned the highest priority possible i.e., `kHighPriTxnUpperBound`, then a single value `Highest priority transaction` is returned without any float.

A transaction's priority is 0 until a transaction is really started.

## Examples

1. Create a table and insert some data.
```sql
CREATE TABLE test (k INT PRIMARY KEY, v INT);
INSERT INTO test VALUES (1, 1);
```

2. Set the lower and upper bound values for your transactions.

```sql
SET yb_transaction_priority_lower_bound = 0.4;
SET yb_transaction_priority_upper_bound = 0.6;
```

3. Create a transaction in the normal-priority bucket

```sql
BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
SELECT yb_get_current_transaction_priority(); -- 0 since a distributed transaction hasn't started
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
SELECT yb_get_current_transaction_priority(); -- still 0 because the read-only operation above doesn't really start a distributed transaction
```

```output
    yb_get_current_transaction_priority
-------------------------------------------
 0.000000000 (Normal priority transaction)
(1 row)
```

```sql
INSERT INTO test VALUES (2, '2'); -- start a distributed txn
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

4. Create a transaction in the high-priority bucket

```sql
BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
SELECT * FROM test WHERE k = 1 FOR UPDATE; -- starts a transaction in high-priority bucket
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

5. Create a transaction with the highest priority

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
