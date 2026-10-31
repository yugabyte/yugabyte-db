---
title: Follower reads
headerTitle: Follower reads
linkTitle: Follower reads
description: Read from a closer replica when your application can tolerate staleness.
headcontent: Read from a closer replica when staleness is acceptable
menu:
  stable:
    identifier: follower-reads
    parent: additional-features
    weight: 70
type: docs
rightNav:
  hideH4: true
---

YugabyteDB normally serves a read from the tablet leader to ensure the result is up to date. Follower reads allow you to serve the read from the closest replica instead, allowing for lower read latencies, at the cost of the read being slightly stale. The result is timeline consistent: writes are not returned out of order. In YSQL, you can specify the staleness of the reads.

Writes still go to the leader. A YSQL follower read returns data as of the current time minus the staleness interval, even when the replica that serves it has newer data.

For an illustration of how this pattern can benefit multi-region applications, see [Follower reads for global applications](../../develop/build-global-apps/follower-reads/). For a walkthrough, see [Follower reads](../../explore/going-beyond-sql/follower-reads-ysql/) in Explore.

A [read replica](../../deploy/multi-dc/read-replica-clusters/) cluster serves reads only as follower reads. With follower reads off, the read goes to the tablet leader in the primary cluster.

## Read time and staleness

A YSQL follower read runs at one consistent read time: the current time minus [yb_follower_read_staleness_ms](../../reference/configuration/yb-tserver/#yb-follower-read-staleness-ms).
YCQL has no staleness setting. A follower serves a YCQL read at its safe time, which is the latest time for which it has all the data. So the result is as old as the follower's lag behind the current time. If that lag is more than `--max_stale_read_bound_time_ms`, the follower rejects the read (region (A) in the following timeline).

## When a follower can serve the read

What happens to a follower read depends on where the follower's safe time falls on this timeline:

```output
  follower safe time falls here:
        │       (A)       │           (B)            │            (C)            │
  ──────┼─────────────────┼──────────────────────────┼───────────────────────────┼──▶ time
                   now − max_stale            now − staleness                   now
  (A) The follower is too far behind. It rejects the read, and the client retries at the leader.
      Applies to YSQL and YCQL.
  (B) YSQL: the follower does not yet have all the data at the read time.
            avoid_waiting = true (default): it rejects the read, and the client retries at the leader.
            avoid_waiting = false: it waits until it catches up, then serves the read.
      YCQL: the follower serves the read at its safe time.
  (C) YSQL: the follower serves the read at now − staleness.
      YCQL: the follower serves the read at its safe time.
```

- `max_stale` is [--max_stale_read_bound_time_ms](../../reference/configuration/yb-tserver/#max-stale-read-bound-time-ms) (default 60 seconds). Raise it if followers fall in (A) too often, for example when they are far from the leader. Setting it to `0` turns off the check.
- `staleness` is [yb_follower_read_staleness_ms](../../reference/configuration/yb-tserver/#yb-follower-read-staleness-ms) (default 30 seconds). A shorter value makes it more likely that a follower is in (B). Don't set it to less than twice [raft_heartbeat_interval_ms](../../reference/configuration/yb-tserver/#raft-heartbeat-interval-ms). On an idle tablet, a follower's safe time advances only when a leader heartbeat arrives, so the safe time can be up to one heartbeat interval behind the current time.
- `avoid_waiting` is [--ysql_follower_reads_avoid_waiting_for_safe_time](../../reference/configuration/yb-tserver/#ysql-follower-reads-avoid-waiting-for-safe-time) (default `true`). Retrying at the leader is usually faster than waiting for the follower to catch up.

A rejected read is always retried at the tablet leader, not at another follower. The `consistent_prefix_failed_reads` metric counts these retries.

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

Avoid using `pg_hint_plan` to mark a statement read only. It can work in some cases, but it relies on side effects and has known issues (see {{<issue 17024>}} and {{<issue 17135>}}).

## YCQL

YCQL follower reads require consistency level `ONE`. Set `ONE` explicitly in your driver, or with `CONSISTENCY ONE` in ycqlsh. For the default level and the other levels, see [CONSISTENCY](../../api/ycqlsh/#consistency). `ONE` changes only reads. YCQL writes stay strongly consistent. The timeline in [When a follower can serve the read](#when-a-follower-can-serve-the-read) shows when a follower serves a YCQL read.

For a full example, see the YCQL tab in [Follower reads](../../explore/going-beyond-sql/follower-reads-ysql/#example) in Explore.
