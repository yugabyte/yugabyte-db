---
title: Explore follower reads in YSQL
headerTitle: Follower reads
linkTitle: Follower reads
description: Learn how you can use follower reads to lower read latencies in local YugabyteDB clusters.
menu:
  stable:
    identifier: explore-multi-region-deployments-follower-reads-ysql
    parent: explore-multi-region-deployments
    weight: 280
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../follower-reads-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>YSQL</a>
  </li>

  <li >
    <a href="../read-replicas-ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>YCQL</a>
  </li>

</ul>

## Introduction

YugabyteDB requires reading from the **leader** to read the latest data. However, for applications that do not require the latest data, and/or are working with unchanging data; the cost of contacting a potentially remote leader to fetch the data may be wasteful. The application could benefit with better latency by reading from a **replica** that may be closer to the client.

Since the replicas may not be up to date with respect to all the updates, it should be noted that this design may respond with stale data. The user may specify how much staleness the application can tolerate. When enabled, read-only operations may be handled by the closest replica; instead of having to go to the leader.

## Surface area and Usage

### Surface area

- *yb_read_from_followers* : This session variable controls whether read from followers is enabled.

- *yb_follower_read_staleness_ms* : This session variable defines the maximum staleness that the user is willing to tolerate.

### Default behavior

- *yb_read_from_followers* : Defaults to false.

- *yb_follower_read_staleness_ms* : Defaults to 30 seconds.

### Expected behavior

The table describes what the expected behavior is when a read happens from a follower.

| Conditions | Expected behavior |
| --- | --- |
| yb_read_from_followers is true AND <br> (Transaction is marked read-only <br>OR<br> Single-statement-select can be inferred to be read-only) <br> | Read will happen from the closest follower|
|yb_read_from_followers is false OR <br> Transaction/statement is not read only | Read will happen from the leader |

### Read from follower conditions

- If the follower’s safe-time is larger than the read-time thus chosen, the follower may serve the read without any delay.

- If the follower is not yet caught up to the specified timestamp, the read will be redirected to a different replica transparently from the end-user. The end user may see a slight increase in latency depending on the location of the replica which satisfies the read.
  - The current behavior is that a follower/replica would wait for the requested timestamp to become safe. This behavior shall be changed to respond back to the `ybclient/pggate` immediately, so that the request may be routed to a different server that has the data.
  - The current redirection policy is to directly go to the leader if the follower read is rejected by a follower. This behavior could be also be changed going forward, to explore other replicas before going to the leader.
  - If the request reaches the leader, it will no longer reject/redirect the request.

### Read-only transaction conditions

Regarding marking the transaction as read only, a user can do one of the following:

- `SET TRANSACTION READ ONLY` : Applies only to the current transaction block.
- `SET SESSION CHARACTERISTICS AS TRANSACTION READ ONLY` : Applies the read-only setting for all statements and transaction blocks that follow.
- `SET default_transaction_read_only = TRUE` : Applies the read-only setting for all statements and transaction blocks that follow.

## Examples

This example uses follower reads since the **transaction** is marked read-only.

```sql
set yb_read_from_followers = true;
start transaction read only;
SELECT * from t WHERE k='k1';
commit;
```

```output
 k  | v
----+----
 k1 | v1
```

This example uses follower reads since the **session** is marked read only.

```sql
set session characteristics as transaction read only;
set yb_read_from_followers = true;
SELECT * from t WHERE k='k1';
```

```output
 k  | v
----+----
 k1 | v1
(1 row)
```

This next two examples use follower reads since **pg_hint** can be used during PREPARE and CREATE FUNCTION to do follower reads.

```sql
set session characteristics as transaction read write;
set yb_read_from_followers = true;

PREPARE select_stmt(text) AS
/*+ Set(transaction_read_only on)*/
SELECT * from t WHERE k=$1;

EXECUTE select_stmt(‘k1’);
```

```output
 k  | v
----+----
 k1 | v1
(1 row)
```

```sql
set session characteristics as transaction read write;
set yb_read_from_followers = true;

CREATE FUNCTION func() RETURNS text AS
$$ /*+ Set(transaction_read_only on)*/
SELECT * from t WHERE k=1 $$ LANGUAGE SQL;
CREATE FUNCTION
SELECT func();
```

```output
 k  | v
----+----
 k1 | v1
(1 row)
```

A join example that uses follower reads.

```sql
set yb_read_from_followers = true;
set session characteristics as transaction read only;
SELECT * from t1, t2 WHERE t1.k='k3' AND t1.v = t2.v;
```

```output
 k | v
---+---
(0 rows)
```

The following examples demonstrate **staleness** after enabling the `yb_follower_read_staleness_ms` property.

```sql
set session characteristics as transaction read write;
select * from t where k = 'k1';
```

```output
 k  | v
----+----
 k1 | v1
(1 row)
```

```sql
UPDATE t SET  v = 'v1+1' where k = 'k1';
/* sleep 10s */

UPDATE t SET  v = 'v1+2' where k = 'k1';
/* sleep 10s */

select * from t where k = 'k1';
```

```output
 k  |  v
----+------
 k1 | v1+2
(1 row)
```

```sql
set session characteristics as transaction read only;
set yb_read_from_followers = true;

select * from t where k = 'k1';
```

```output
 k  |  v
----+------
 k1 | v1
(1 row)
```

```sql
set yb_follower_read_staleness_ms = 5000;

select * from t where k = 'k1';   /* 5s old value */
```

```output
 k  |  v
----+------
 k1 | v1+2
(1 row)
```

```sql
set yb_follower_read_staleness_ms = 15000;

select * from t where k = 'k1';   /* 15s old value */
```

```output
 k  |  v
----+------
 k1 | v1+1
(1 row)
```

```sql
postgres=# set yb_follower_read_staleness_ms = 25000;
postgres=# select * from t where k = 'k1';   /* 25s old value */
```

```output
 k  |  v
----+------
 k1 | v1
(1 row)
```

The final example demonstrates that SELECTS error out in read-only transactions.

```sql
set yb_read_from_followers = true;
set session characteristics as transaction read only;

SELECT * INTO bar2 from t WHERE k='k2';
```

```output
ERROR:  cannot execute SELECT INTO in a read-only transaction
```
