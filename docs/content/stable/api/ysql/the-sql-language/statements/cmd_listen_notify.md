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

LISTEN/NOTIFY is available in v2025.2.3 and later.

## Synopsis

- `LISTEN` registers the current session as a listener on a channel.
- `NOTIFY` (or the `pg_notify()` function) sends a notification on a channel, optionally with a payload.
- `UNLISTEN` removes one or more listener registrations for the current session.

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

## Syntax and SQL reference

YSQL follows the same SQL syntax as PostgreSQL. For complete syntax, refer to the PostgreSQL documentation:

- [NOTIFY](https://www.postgresql.org/docs/15/sql-notify.html)
- [LISTEN](https://www.postgresql.org/docs/15/sql-listen.html)
- [UNLISTEN](https://www.postgresql.org/docs/15/sql-unlisten.html)

Client library interaction (for example, polling for notifications via libpq or JDBC) is the same as in PostgreSQL.

## Enable LISTEN/NOTIFY

LISTEN/NOTIFY is disabled by default. To enable it, set the [ysql_yb_enable_listen_notify](../../../../../reference/configuration/yb-tserver/#ysql-yb-enable-listen-notify) flag to `true` on both TServers and Masters.

After you enable the feature, the leader Master creates internal objects (including the `yb_system` database and the `yb_system.pg_yb_notifications` table) in the background. If you run `LISTEN` or `NOTIFY` before those objects exist, you may see an error asking you to retry shortly; waiting a few seconds and retrying is expected during startup or right after turning the feature on.

## How notifications are delivered in YugabyteDB

In vanilla PostgreSQL, notifications are written through shared memory. In YugabyteDB, `NOTIFY` temporarily stores the notifications in the `yb_system.pg_yb_notifications` table. A per-node _notifications poller_ reads that table using CDC-style logical replication (an internal logical replication slot on each TServer) and delivers the notifications to local listeners.

Each TServer creates a named logical replication slot derived from the node identity (`yb_notifications_<tserver-uuid>`). Do not drop or repurpose that slot for other work.

Because the poller relies on logical replication internally, LISTEN/NOTIFY also depends on the following TServer flags. These flags all have defaults that are compatible with LISTEN/NOTIFY, so no action is needed unless you have explicitly changed them.

| TServer flag | Default | Effect on LISTEN/NOTIFY |
| :------------- | :------ | :---------------------- |
| `ysql_yb_enable_replication_commands` | `true` | LISTEN fails if set to `false`. |
| `ysql_yb_enable_replication_slot_consumption` | `true` | LISTEN fails if set to `false`. |
| `ysql_yb_allow_replication_slot_lsn_types` | `true` | LISTEN fails if set to `false`. |
| `ysql_yb_allow_replication_slot_ordering_modes` | `false` | LISTEN fails if set to `true`. |
| `max_replication_slots` | `10` | LISTEN/NOTIFY creates one internal replication slot per node. Ensure this limit is large enough to accommodate it alongside any user-created replication slots. |


## Differences from PostgreSQL

| Aspect | PostgreSQL | YugabyteDB |
| :----- | :--------- | :--------- |
| Sender identity | The notification message contains the sender's PID. A listener can compare this PID with its own backend PID to ignore self-notifications. | The notification message still carries a PID, but in a multi-node cluster different TServers can have backends with the same PID. To reliably identify the sender, include the TServer UUID in the notification payload. A session can obtain its TServer UUID from the `uuid` column of `yb_servers()`. |
| `pg_notification_queue_usage()` | Returns the usage fraction of the server's notification queue. | Returns the usage fraction of the _local TServer's_ notification queue. |
| Behavior when the queue is full | New `NOTIFY` calls fail (return an error) until the queue drains. | When new notifications are waiting to be written to the queue, and at least one listener is fully caught up (that is, waiting for new notifications), the _slowest listener_ on that TServer is terminated. This allows the queue tail to advance, making space for new notifications and unblocking the caught up listener(s). Applications must handle disconnections by reconnecting and re-issuing `LISTEN`. |

## Advanced tuning

The following flags control how frequently the notifications poller checks for new notifications.

| tserver flag | Default | Description |
| :------------- | :------ | :---------- |
| `yb_notifications_poll_sleep_duration_nonempty_ms` | `1` | Wait time in milliseconds before the next poll when the previous poll returned data. |
| `yb_notifications_poll_sleep_duration_empty_ms` | `100` | Wait time in milliseconds before the next poll when the previous poll returned no data. |
