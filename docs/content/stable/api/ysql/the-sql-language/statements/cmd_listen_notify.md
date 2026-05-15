---
title: LISTEN, NOTIFY, and UNLISTEN [YSQL]
headerTitle: LISTEN, NOTIFY, and UNLISTEN
linkTitle: LISTEN, NOTIFY, and UNLISTEN
description: Use LISTEN, NOTIFY, and UNLISTEN for asynchronous notification between database sessions in YSQL.
tags:
  feature: early-access
menu:
  stable_api:
    identifier: cmd_listen_notify
    parent: statements
type: docs
---

{{<tags/feature/ea idea="1901">}}LISTEN/NOTIFY provides a communication mechanism for a collection of processes accessing the same YSQL database.

It is available in v2025.2.3 and later.

## Synopsis

- `LISTEN` registers the current session as a listener on a channel.
- `NOTIFY` (or the `pg_notify()` function) sends a notification on a channel, optionally with a payload.
- `UNLISTEN` removes one or more listener registrations for the current session.

## Syntax

YSQL follows the same SQL syntax as PostgreSQL. For complete syntax, refer to the PostgreSQL documentation:

- [NOTIFY](https://www.postgresql.org/docs/15/sql-notify.html)
- [LISTEN](https://www.postgresql.org/docs/15/sql-listen.html)
- [UNLISTEN](https://www.postgresql.org/docs/15/sql-unlisten.html)

Client library interaction (for example, polling for notifications via libpq or JDBC) is the same as in PostgreSQL.

{{< note title="Note" >}}
The sending and receiving sessions must use the same `client_encoding`.
{{< /note >}}

## Examples

```sql
LISTEN virtual;
NOTIFY virtual;
```

```output
Asynchronous notification "virtual" received from server process with PID 8448.
```

```sql
NOTIFY virtual, 'This is the payload';
```

```output
Asynchronous notification "virtual" with payload "This is the payload" received from server process with PID 8448.
```

```sql
LISTEN foo;
SELECT pg_notify('fo' || 'o', 'pay' || 'load');
```

```output
Asynchronous notification "foo" with payload "payload" received from server process with PID 8448.
```

## Transaction semantics

As in PostgreSQL, `LISTEN` and `NOTIFY` take effect at transaction commit (no effect if the transaction aborts). Duplicate notifications (same channel and payload) within a transaction are coalesced. Notifications are delivered in the order they were sent within a transaction, and across transactions in commit order. A listener receives notifications from transactions that commit after its own `LISTEN` commits, though it may receive some additional notifications that committed just before. If a listening session is inside a transaction, incoming notifications are held until the transaction completes (either commits or aborts).

## Enable LISTEN/NOTIFY

The feature is disabled by default. To enable it, set the [ysql_yb_enable_listen_notify](../../../../../reference/configuration/yb-tserver/#ysql-yb-enable-listen-notify) flag to `true` on both TServers and Masters.

After you enable the feature, the leader Master creates internal objects (including the `yb_system` database and the `yb_system.pg_yb_notifications` table) in the background. If you run `LISTEN` or `NOTIFY` before those objects exist, you may see an error asking you to retry shortly; waiting a few seconds and retrying is expected during startup or right after turning the feature on.

{{< warning >}}
The `yb_system` database and the `yb_system.pg_yb_notifications` table are for internal use only. Do not modify or drop them.
{{< /warning >}}

## How notifications are delivered in YugabyteDB

In vanilla PostgreSQL, notifications are written through shared memory. In YugabyteDB, `NOTIFY` temporarily stores the notifications in the `yb_system.pg_yb_notifications` table. A per-node _notifications poller_ reads that table using CDC-style logical replication (an internal logical replication slot on each TServer) and delivers the notifications to local listeners.

Each TServer creates a named logical replication slot derived from the node identity (`yb_notifications_<tserver-uuid>`).

{{< warning >}}
Do not drop or repurpose the `yb_notifications_*` replication slots. They are required for notification delivery.
{{< /warning >}}

Because the poller relies on logical replication internally, notification delivery also depends on the following TServer flags. These flags all have defaults that are compatible with the feature, so no action is needed unless you have explicitly changed them.

| TServer flag | Default | Effect on LISTEN/NOTIFY |
| :------------- | :------ | :---------------------- |
| `ysql_yb_enable_replication_commands` | `true` | LISTEN fails if set to `false`. |
| `ysql_yb_enable_replication_slot_consumption` | `true` | LISTEN fails if set to `false`. |
| `ysql_yb_allow_replication_slot_lsn_types` | `true` | LISTEN fails if set to `false`. |
| `ysql_yb_allow_replication_slot_ordering_modes` | `false` | LISTEN fails if set to `true`. |
| `max_replication_slots` | `10` | The notification system creates one internal replication slot per node. Ensure this limit is large enough to accommodate it alongside any user-created replication slots. |
| `cdc_max_virtual_wal_per_tserver` | `5` | The notification system creates one virtual WAL per node. Ensure this limit is large enough to accommodate it alongside any user-created CDC streams. |

## Differences from PostgreSQL

| Aspect | PostgreSQL | YugabyteDB |
| :----- | :--------- | :--------- |
| Sender identity | The notification message contains the sender's PID. A listener can compare this PID with its own backend PID to ignore self-notifications. | The notification message still carries a PID, but in a multi-node cluster different TServers can have backends with the same PID. To reliably identify the sender, include the TServer UUID in the notification payload. A session can obtain its TServer UUID from the `uuid` column of `yb_servers()`. |
| `pg_notification_queue_usage()` | Returns the usage fraction of the server's notification queue. | Returns the usage fraction of the _local TServer's_ notification queue. |
| Behavior when the queue is full | New `NOTIFY` calls fail (return an error) until the queue drains. | When new notifications are waiting to be written to the queue, and at least one listener is fully caught up (that is, waiting for new notifications), the _slowest listener_ on that TServer is terminated. This allows the queue tail to advance, making space for new notifications and unblocking the caught up listener(s). Applications must handle disconnections by reconnecting and re-issuing `LISTEN`. |

## Observability

### pg_listening_channels()

Returns the set of channel names that the current session is listening on.

```sql
LISTEN channel1;
LISTEN channel2;
SELECT * FROM pg_listening_channels();
```

```output
 pg_listening_channels
-----------------------
 channel1
 channel2
```

### pg_notification_queue_usage()

Returns the fraction of the notification queue that is currently occupied by pending notifications on the local TServer.

```sql
SELECT pg_notification_queue_usage();
```

```output
 pg_notification_queue_usage
-----------------------------
                          0.2
```

### Replication slot monitoring

Because notification delivery uses CDC internally, you can query `pg_replication_slots` to inspect the notification-related replication slots:

```sql
SELECT slot_name, active_pid, yb_stream_id, yb_restart_time
FROM pg_replication_slots
WHERE slot_name LIKE 'yb_notifications_%';
```

Each row shows the replication slot name (which includes the TServer UUID as a suffix), the notifications poller process ID, the CDC stream ID, and the time up to which the poller has caught up.

For additional CDC-level metrics, refer to [Monitor CDC metrics](../../../../../additional-features/change-data-capture/using-logical-replication/monitor/).

## Latency

Notification delivery is asynchronous. In steady state, expect a latency of up to 100-150 ms between a `NOTIFY` and its delivery to listeners, depending on the deployment topology. Network partitions or high cluster load will increase this latency.

The following TServer flags control the internal polling frequency:

| TServer flag | Default | Description |
| :------------- | :------ | :---------- |
| `yb_notifications_poll_sleep_duration_nonempty_ms` | `1` | Wait time in milliseconds before the next poll when the previous poll returned notifications. |
| `yb_notifications_poll_sleep_duration_empty_ms` | `100` | Wait time in milliseconds before the next poll when the previous poll returned no notifications. |

An aggressive polling frequency reduces delivery latency at the cost of higher CPU usage, and vice versa.

## Scale

This is designed for lightweight signaling — up to a few thousand `NOTIFY` calls per second.

The number of listeners scales horizontally with the cluster.

{{< tip title="Tip" >}}
If you have a small number of listening sessions and can control which nodes they connect to, co-locating them on the same TServer reduces the fan-out of notifications across the cluster.
{{< /tip >}}

## Cross-feature implications

### Connection manager

Listening sessions require a dedicated backend process. If you are using [YSQL Connection Manager](../../../../../additional-features/connection-manager-ysql/), `LISTEN` automatically makes the session [sticky](../../../../../additional-features/connection-manager-ysql/ycm-setup/#sticky-connections) for the rest of its lifetime.

