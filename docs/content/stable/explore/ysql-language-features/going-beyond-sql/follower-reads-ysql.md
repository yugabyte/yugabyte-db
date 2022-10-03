---
title: Explore follower reads in YSQL
headerTitle: Follower reads
linkTitle: Follower reads
description: Learn how you can use follower reads to lower read latencies in local YugabyteDB clusters.
menu:
  stable:
    identifier: follower-reads-ysql
    parent: going-beyond-sql
    weight: 280
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../follower-reads-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>YSQL</a>
  </li>

  <li >
    <a href="../follower-reads-ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>YCQL</a>
  </li>
</ul>

## Leader leases

In a distributed environment, when one node in a cluster is elected as the leader holding the latest data, it is possible that another node may assume that it is the leader, and that it holds the latest data. This could result in serving stale reads to a client. To avoid this confusion, YugabyteDB provides a _leader lease_ mechanism  where an elected node member is guaranteed to be the leader until its lease expires.

The leader lease mechanism guarantees to serve strongly consistent reads where a client can fetch reads directly from the leader, because the leader under lease will have the latest data.
The interactive animations in this [blog post](https://blog.yugabyte.com/low-latency-reads-in-geo-distributed-sql-with-raft-leader-leases/) explains the performance improvements using leader leases.

## Follower reads

YugabyteDB requires reading from the **leader** to read the latest data. However, for applications that don't require the latest data, or are working with unchanging data, the cost of contacting a potentially remote leader to fetch the data may be wasteful. Your application may benefit from better latency by reading from a **replica** that is closer to the client.

### Time-critical use cases

Let's say a user starts a donation page to raise money for a personal cause and the target amount must be met by the end of the day. In a time-critical scenario, the user benefits from accessing the most recent donation made on the page and can keep track of the progress. Such real-time applications require the latest information as soon as it's available. But in the case of an application where fetching slightly stale data is okay, follower reads can be helpful.

### Latency-tolerant (staleness) use cases

Let's say a social media post gets about a million likes and more continuously. For a post with massive likes such as this one, slightly stale reads are acceptable, and immediate updates aren't necessary because the absolute number may not really matter to the user reading the post. Such applications don't need to always make requests directly to the leader. Instead, a slightly older value from the closest replica can achieve improved performance with lower latency.

Follower reads are applicable for applications that can tolerate staleness. Replicas may not be completely up to date with all updates, so this design may respond with stale data. You can specify how much staleness the application can tolerate. When enabled, read-only operations may be handled by the closest replica, instead of having to go to the leader. The GUC session variables that PostgreSQL supports can be used to enable follower reads.

## Surface area and usage

### Surface area

Two session variables control the behavior of follower reads:

- `yb_read_from_followers` controls whether reading from followers is enabled. Default is false.
- `yb_follower_read_staleness_ms` sets the maximum allowable staleness. Default is 30000 (30 seconds).

### Expected behavior

The table describes what the expected behavior is when a read happens from a follower.

| Conditions | Expected behavior |
| :--------- | :---------------- |
| yb_read_from_followers is true AND transaction is marked read-only | Read happens from the closest follower |
| yb_read_from_followers is false OR transaction/statement is not read-only | Read happens from the leader |

### Read from follower conditions

- If the follower's safe-time is at least `<current_time> - <staleness>`, the follower may serve the read without any delay.

- If the follower is not yet caught up to `<current_time> - <staleness>`, the read will be redirected to a different replica transparently from the end-user. The end user may see a slight increase in latency depending on the location of the replica which satisfies the read.

### Read-only transaction conditions

To mark a transaction as read only, a user can do one of the following:

- `SET TRANSACTION READ ONLY` applies only to the current transaction block.
- `SET SESSION CHARACTERISTICS AS TRANSACTION READ ONLY` applies the read-only setting to all statements and transaction blocks that follow.
- `SET default_transaction_read_only = TRUE` applies the read-only setting to all statements and transaction blocks that follow.
- Use the **pg_hint_plan** mechanism to embed the hint along with the `SELECT` statement. For example, `/*+ Set(transaction_read_only true) */ SELECT ...` applies only to the current `SELECT` statement.

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

The following examples use follower reads since the **pg_hint_plan** mechanism is used during SELECT, PREPARE, and CREATE FUNCTION to perform follower reads.

{{< note title="Note" >}}
The pg_hint_plan hint needs to be applied at the prepare/function-definition stage and not at the `execute` stage.
{{< /note >}}

```sql
set yb_read_from_followers = true;
/*+ Set(transaction_read_only on) */
SELECT * from t WHERE k='k1';
```

```output
----+----
 k1 | v1
(1 row)
```

```sql
set yb_read_from_followers = true;
PREPARE select_stmt(text) AS
/*+ Set(transaction_read_only on) */
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
set yb_read_from_followers = true;
CREATE FUNCTION func() RETURNS text AS
$$ /*+ Set(transaction_read_only on) */
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

A **join** example that uses follower reads.

```sql
create table table1(k int primary key, v int);
create table table2(k int primary key, v int);
insert into table1 values (1, 2), (2, 4), (3, 6), (4,8);
insert into table2 values (1, 3), (2, 6), (3, 9), (4,12);
set yb_read_from_followers = true;
set session characteristics as transaction read only;
select * from table1, table2 where table1.k = 3 and table2.v = table3.v;
```

```output
 k | v | k | v
---+---+---+---
 3 | 6 | 2 | 6
(1 row)
```

The following examples demonstrate **staleness** after enabling the `yb_follower_read_staleness_ms` property.

```sql
set session characteristics as transaction read write;
insert into t values ('k1', 'v1')
/* sleep 10s */
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

This selects the latest version of the row because the transaction setting for the session is `read write`.

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
select * from t where k = 'k1';   /* up to 5s old value */
```

```output
 k  |  v
----+------
 k1 | v1+2
(1 row)
```

```sql
set yb_follower_read_staleness_ms = 15000;
select * from t where k = 'k1';   /* up to 15s old value */
```

```output
 k  |  v
----+------
 k1 | v1+1
(1 row)
```

```sql
set yb_follower_read_staleness_ms = 25000;
select * from t where k = 'k1';   /* up to 25s old value */
```

```output
 k  |  v
----+------
 k1 | v1
(1 row)
```
