---
title: Explore follower reads - YSQL
headerTitle: Follower reads
linkTitle: Follower reads
description: Learn how to use follower reads to lower read latencies in local YugabyteDB clusters in YSQL.
menu:
  stable:
    identifier: follower-reads-ysql
    parent: going-beyond-sql
    weight: 100
type: docs
---

{{<api-tabs>}}

YugabyteDB requires reading from the leader to read the latest data. However, for applications that do not require the latest data or are working with unchanging data, the cost of contacting a potentially remote leader to fetch the data may be wasteful. Your application may benefit from better latency by reading from a replica that is closer to the client.

{{<lead link="../../../additional-features/follower-reads/">}}
For how a read chooses a replica, how stale the result is, and how to turn follower reads on, see [Follower reads](../../../additional-features/follower-reads/).
{{</lead>}}

## Use cases

1. Time-critical. Suppose the end-user starts a donation page to raise money for a personal cause and the target amount must be met by the end of the day. In a time-critical scenario, the end-user benefits from accessing the most recent donation made on the page and can keep track of the progress. Such real-time applications require the latest information as soon as it is available. But in the case of an application where fetching slightly stale data is acceptable, follower reads can be helpful.

1. Latency-tolerant (staleness). Suppose a social media post gets a million likes and more continuously. For a post with massive likes such as this one, slightly stale reads are acceptable, and immediate updates are not necessary because the absolute number may not really matter to the end-user reading the post. Such applications do not need to always make requests directly to the leader. Instead, a slightly older value from the closest replica can achieve improved performance with lower latency.

Follower reads are applicable for applications that can tolerate staleness. Replicas may not be completely up-to-date with all updates, so this design may respond with stale data. You can specify how much staleness the application can tolerate. When enabled, read-only operations may be handled by the closest replica, instead of having to go to the leader.

## Leaders and leader leases

In a distributed environment, when one node in a cluster is elected as the leader holding the latest data, another node may assume that it is the leader, and that it holds the latest data. This could result in serving stale reads to a client. To avoid this confusion, YugabyteDB provides a [leader lease mechanism](../../../architecture/transactions/single-row-transactions/#leader-leases-reading-the-latest-data-in-case-of-a-network-partition) where an elected node member is guaranteed to be the leader until its lease expires.

The leader lease mechanism guarantees to serve strongly consistent reads where a client can fetch reads directly from the leader, because the leader under lease will have the latest data.

For an illustration of the performance improvements using leader leases, see [Low Latency Reads in Geo-Distributed SQL with Raft Leader Leases](https://www.yugabyte.com/blog/low-latency-reads-in-geo-distributed-sql-with-raft-leader-leases/).

## Examples

This example uses follower reads because the transaction is marked read-only:

```sql
SET yb_read_from_followers = true;
START TRANSACTION READ ONLY;
SELECT * FROM t WHERE k='k1';
COMMIT;
SET yb_read_from_followers = false;
```

```output
 k  | v
----+----
 k1 | v1
```

Use SET LOCAL inside a transaction to have follower reads turned on only for the transaction. Set `yb_read_from_followers` before any statement is executed in the block:

```sql
START TRANSACTION READ ONLY;
SET LOCAL yb_read_from_followers = true;
SELECT * FROM t WHERE k='k1';
COMMIT;
```

```output
 k  | v
----+----
 k1 | v1
```

This example uses follower reads because the session is marked read only:

```sql
SET SESSION CHARACTERISTICS AS TRANSACTION READ ONLY;
SET yb_read_from_followers = true;
SELECT * FROM t WHERE k='k1';
```

```output
 k  | v
----+----
 k1 | v1
(1 row)
```

The following is a JOIN example that uses follower reads:

```sql
CREATE TABLE table1(k int primary key, v int);
CREATE TABLE table2(k int primary key, v int);
INSERT INTO table1 VALUES (1, 2), (2, 4), (3, 6), (4,8);
INSERT INTO table2 VALUES (1, 3), (2, 6), (3, 9), (4,12);
SET yb_read_from_followers = true;
SET SESSION CHARACTERISTICS AS TRANSACTION READ ONLY;
SELECT * FROM table1, table2 WHERE table1.k = 3 AND table2.v = table3.v;
```

```output
 k | v | k | v
---+---+---+---
 3 | 6 | 2 | 6
(1 row)
```

### Follower read staleness

The following examples demonstrate staleness in a single session after enabling the `yb_follower_read_staleness_ms` property.

First, set the session characteristics; the READ WRITE setting is the default, and shown here for clarity only.

```sql
SET SESSION CHARACTERISTICS AS TRANSACTION READ WRITE;
INSERT INTO t VALUES ('k1', 'v1')
/* sleep 10s */
SELECT * FROM t WHERE k = 'k1';
```

```output
 k  | v
----+----
 k1 | v1
(1 row)
```

Next run some updates separated by sleep statements of 10 seconds each.

```sql
UPDATE t SET  v = 'v1+1' WHERE k = 'k1';
/* sleep 10s */
UPDATE t SET  v = 'v1+2' WHERE k = 'k1';
/* sleep 10s */
```

This query displays the latest version of the row because the transaction setting for the session is still READ WRITE.

```sql
SELECT * FROM t WHERE k = 'k1';
```

```output
 k  |  v
----+------
 k1 | v1+2
(1 row)
```

Next, set the transaction to READ ONLY and enable follower reads. The default value for `yb_follower_read_staleness_ms` is 30000 (30 seconds), so this query will display the version of the row from 30 seconds ago.

```sql
SET SESSION CHARACTERISTICS AS TRANSACTION READ ONLY;
SET yb_read_from_followers = true;
SELECT * FROM t WHERE k = 'k1';
```

```output
 k  |  v
----+------
 k1 | v1
(1 row)
```

Set the `yb_follower_read_staleness_ms` property to 5000 (5 seconds) and run the query again. The query will display the version of the row from 5 seconds ago.

```sql
SET yb_follower_read_staleness_ms = 5000;
SELECT * FROM t WHERE k = 'k1';
```

```output
 k  |  v
----+------
 k1 | v1+2
(1 row)
```

Next, set `yb_follower_read_staleness_ms` to 15000 (15 seconds) and run the query again. This query will display the version of the row from 15 seconds ago.

```sql
SET yb_follower_read_staleness_ms = 15000;
SELECT * FROM t WHERE k = 'k1';
```

```output
 k  |  v
----+------
 k1 | v1+1
(1 row)
```

Finally, set `yb_follower_read_staleness_ms` to 25000 (25 seconds) and run the query again. This query will display the version of the row from 25 seconds ago.

```sql
SET yb_follower_read_staleness_ms = 25000;
SELECT * FROM t WHERE k = 'k1';
```

```output
 k  |  v
----+------
 k1 | v1
(1 row)
```

Note that your results may differ slightly depending on how quickly you run the queries.
