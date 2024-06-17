---
title: Explore follower reads - YSQL
headerTitle: Follower reads
linkTitle: Follower reads
description: Learn how to use follower reads to lower read latencies in local YugabyteDB clusters in YSQL.
menu:
  preview:
    identifier: follower-reads-ysql
    parent: going-beyond-sql
    weight: 100
type: docs
---

{{<tabs>}}
{{<tabitem href="../follower-reads-ysql/" text="YSQL" icon="postgres" active="true">}}
{{<tabitem href="../follower-reads-ycql/" text="YCQL" icon="cassandra" >}}
{{</tabs>}}

YugabyteDB requires reading from the leader to read the latest data. However, for applications that do not require the latest data or are working with unchanging data, the cost of contacting a potentially remote leader to fetch the data may be wasteful. Your application may benefit from better latency by reading from a replica that is closer to the client.

## Use cases

1. **Time-critical** : Suppose the end-user starts a donation page to raise money for a personal cause and the target amount must be met by the end of the day. In a time-critical scenario, the end-user benefits from accessing the most recent donation made on the page and can keep track of the progress. Such real-time applications require the latest information as soon as it is available. But in the case of an application where fetching slightly stale data is acceptable, follower reads can be helpful.

1. **Latency-tolerant (staleness)** : Suppose a social media post gets a million likes and more continuously. For a post with massive likes such as this one, slightly stale reads are acceptable, and immediate updates are not necessary because the absolute number may not really matter to the end-user reading the post. Such applications do not need to always make requests directly to the leader. Instead, a slightly older value from the closest replica can achieve improved performance with lower latency.

Follower reads are applicable for applications that can tolerate staleness. Replicas may not be completely up-to-date with all updates, so this design may respond with stale data. You can specify how much staleness the application can tolerate. When enabled, read-only operations may be handled by the closest replica, instead of having to go to the leader.

## Leaders and leader leases

In a distributed environment, when one node in a cluster is elected as the leader holding the latest data, another node may assume that it is the leader, and that it holds the latest data. This could result in serving stale reads to a client. To avoid this confusion, YugabyteDB provides a [leader lease mechanism](../../../architecture/transactions/single-row-transactions/#leader-leases-reading-the-latest-data-in-case-of-a-network-partition) where an elected node member is guaranteed to be the leader until its lease expires.

The leader lease mechanism guarantees to serve strongly consistent reads where a client can fetch reads directly from the leader, because the leader under lease will have the latest data.

For an illustration of the performance improvements using leader leases, see [Low Latency Reads in Geo-Distributed SQL with Raft Leader Leases](https://www.yugabyte.com/blog/low-latency-reads-in-geo-distributed-sql-with-raft-leader-leases/).

## Configuration parameters

Two YSQL configuration parameters control the behavior of follower reads:

- `yb_read_from_followers` controls whether or not reading from followers is enabled. The default value is false.

- `yb_follower_read_staleness_ms` sets the maximum allowable staleness. The default value is 30000 (30 seconds).

  Although the default is recommended, you can set the staleness to a shorter value. The tradeoff is the shorter the staleness, the more likely some reads may be redirected to the leader if the follower isn't sufficiently caught up. You shouldn't set `yb_follower_read_staleness_ms` to less than 2x the [raft_heartbeat_interval_ms](../../../reference/configuration/yb-tserver/#raft-heartbeat-interval-ms) (which by default is 500 ms).

Note that even if the tablet leader is on the closest node, you would still read from `Now()-yb_follower_read_staleness_ms`. Therefore, when follower reads are used, the read is always stale, even if you are reading from a tablet leader.

## Expected behavior

The following table provides information on the expected behavior when a read happens from a follower.

| Conditions | Expected behavior |
| :--------- | :---------------- |
| `yb_read_from_followers` is true AND transaction is marked read-only | Read happens from the closest replica of the tablet, which could be leader or follower. |
| `yb_read_from_followers` is false OR transaction or statement is not read-only | Read happens from the leader. |

## Read-only transaction

You can mark a transaction as read-only by applying the following guidelines:

- `SET TRANSACTION READ ONLY` applies only to the current transaction block.
- `SET SESSION CHARACTERISTICS AS TRANSACTION READ ONLY` applies the read-only setting to all statements and transaction blocks that follow.
- `SET default_transaction_read_only = TRUE` applies the read-only setting to all statements and transaction blocks that follow.

Note: The use of `pg_hint_plan` to mark a statement as read-only is not recommended. It may work in some cases, but relies on side effects and has known issues (see [GH17024](https://github.com/yugabyte/yugabyte-db/issues/17024) and  [GH17135](https://github.com/yugabyte/yugabyte-db/issues/17135)).

## Caveats

- If the follower's safe-time is at least `<current_time> - <staleness>`, the follower may serve the read without any delay.

- If the follower is not yet caught up to `<current_time> - <staleness>`, the read is redirected to a different replica transparently from the end-user. The end-user may see a slight increase in latency depending on the location of the replica which satisfies the read.


## Examples

This example uses follower reads because the transaction is marked read-only:

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

This example uses follower reads because the session is marked read-only:

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

The following is a `JOIN` example that uses follower reads:

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

The following examples demonstrate staleness after enabling the `yb_follower_read_staleness_ms` property:

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
