---
title: Follower reads
headerTitle: Follower reads
linkTitle: Follower reads
description: Read from a closer replica when your application can tolerate staleness.
headcontent: Read from a closer replica when staleness is acceptable
menu:
  v2025.2:
    identifier: follower-reads
    parent: additional-features
    weight: 70
type: docs
rightNav:
  hideH4: true
---

YugabyteDB normally serves a read from the tablet leader to ensure the result is up to date. Follower reads allow you to serve the read from the closest replica instead, allowing for lower read latencies, at the cost of the read being slightly stale. The result is timeline consistent: writes are not returned out of order. You can specify the staleness of the reads.

Writes still go to the leader. A follower read is stale even when the closest replica is the leader, because the read time is `now` minus the staleness interval.

For an illustration of how this pattern can benefit multi-region applications, see [Follower reads for global applications](../../develop/build-global-apps/follower-reads/). For a walkthrough, see [Follower reads](../../explore/going-beyond-sql/follower-reads-ysql/) in Explore.

[Read replica](../../deploy/multi-dc/read-replica-clusters/) clusters also use follower reads.

## How far behind a follower may be

When the follower's safe time is further behind now than [--max_stale_read_bound_time_ms](../../reference/configuration/yb-tserver/#max-stale-read-bound-time-ms) (default 60000, 60 seconds), the read is forwarded to the leader. This applies to YSQL follower reads and to YCQL reads at consistency `ONE`. Raise the flag when the follower and the leader are far apart.

## YSQL

### Enable follower reads

A read uses a follower only when both of the following are true:

- [yb_read_from_followers](../../reference/configuration/yb-tserver/#yb-read-from-followers) is `true`. The default is `false`.
- The transaction or session is [read only](../../api/ysql/the-sql-language/statements/txn_set/#read-only-mode).

| Conditions | Where the read runs |
| :--------- | :------------------ |
| `yb_read_from_followers` is true and the transaction is read only | The closest replica, which can be a leader or a follower. |
| `yb_read_from_followers` is false, or the transaction or statement is not read only | The leader. |

Set `yb_read_from_followers` for the session, or for one transaction with `SET LOCAL`. With `SET LOCAL`, set it before the first statement in the transaction block, and not inside a subtransaction (a savepoint).

### Read-only transaction

Mark the transaction read only in one of these ways:

- [SET TRANSACTION READ ONLY](../../api/ysql/the-sql-language/statements/txn_set/#read-only-mode) applies to the current transaction block.
- `SET SESSION CHARACTERISTICS AS TRANSACTION READ ONLY` applies to later statements and transaction blocks in the session.
- [SET default_transaction_read_only = TRUE](../../reference/configuration/yb-tserver/#default-transaction-read-only) applies to later statements and transaction blocks in the session.

Avoid using `pg_hint_plan` to mark a statement read only. It can work in some cases, but it relies on side effects and has known issues. See [GH17024](https://github.com/yugabyte/yugabyte-db/issues/17024) and [GH17135](https://github.com/yugabyte/yugabyte-db/issues/17135).

### Staleness

[yb_follower_read_staleness_ms](../../reference/configuration/yb-tserver/#yb-follower-read-staleness-ms) sets how far in the past the read runs. The default is 30000 (30 seconds). The read executes at hybrid time of about `now` minus this value. YSQL returns no data newer than this interval, even when a local replica has it, so the result stays consistent. YCQL uses a [looser consistency model](#ycql).

A shorter interval makes it more likely that a follower is behind and the read is redirected to the leader. Set the value to at least twice the maximum clock skew ([--max_clock_skew_usec](../../reference/configuration/yb-tserver/#max-clock-skew-usec), 500 ms by default). YugabyteDB rejects a smaller value.

Because the read time is always in the past, the result is stale even when the closest replica is the tablet leader.

The `consistent_prefix_failed_reads` metric counts follower reads that could not be served by the closest replica and were retried at the leader.

### When a follower can serve the read

A follower serves the read only when both of the following are true:

- The follower is no further behind now than [--max_stale_read_bound_time_ms](#how-far-behind-a-follower-may-be). This is checked on every follower read and doesn't depend on `yb_follower_read_staleness_ms`. A follower that is further behind rejects the read and the client retries it at the leader. Setting the flag to `0` turns off this check.
- The follower's safe time has reached the read time (`now` minus `yb_follower_read_staleness_ms`).

If the follower hasn't reached the read time, the behavior depends on [--ysql_follower_reads_avoid_waiting_for_safe_time](../../reference/configuration/yb-tserver/#ysql-follower-reads-avoid-waiting-for-safe-time):

- `true` (default): when the client specified the read time, the follower rejects the read and the client retries it at the leader, which is typically faster than waiting. Latency then depends on how far the leader is from the client.
- `false`: the follower waits for its safe time to catch up, then serves the read.

When the follower's safe time is at least `current_time - staleness` and within [--max_stale_read_bound_time_ms](#how-far-behind-a-follower-may-be) of now, the follower can serve the read without waiting. If `yb_follower_read_staleness_ms` is greater than that bound, a follower whose safe time reaches the read time still sends the read to the leader.

When the follower is behind that time, the read is redirected to another replica. Latency then depends on where that replica is.

[--ysql_follower_reads_avoid_waiting_for_safe_time](../../reference/configuration/yb-tserver/#ysql-follower-reads-avoid-waiting-for-safe-time) (default `true`) rejects a follower read whose read time is not yet safe on that follower. The read goes to the leader instead of waiting for safe time to catch up.

## YCQL

YCQL follower reads require consistency level `ONE`. The default is `QUORUM`. `ONE` changes only reads. YCQL writes stay strongly consistent. In [ycqlsh](../../api/ycqlsh/), set it with `CONSISTENCY ONE`. See [CONSISTENCY](../../api/ycqlsh/#consistency). These reads use the same [staleness bound](#how-far-behind-a-follower-may-be).

For a walkthrough, see [Follower reads](../../explore/going-beyond-sql/follower-reads-ysql/) in Explore.
