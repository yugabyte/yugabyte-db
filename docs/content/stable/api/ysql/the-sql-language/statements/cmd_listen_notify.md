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

LISTEN/NOTIFY provides a communication mechanism for a collection of processes accessing the same YSQL database.

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

## Enabling LISTEN/NOTIFY in YugabyteDB

LISTEN/NOTIFY is **disabled by default**. You must enable it on **both** `yb-master` and `yb-tserver` by setting `ysql_yb_enable_listen_notify` to `true` (see [yb-tserver](../../../../../reference/configuration/yb-tserver/#ysql-yb-enable-listen-notify) and [yb-master](../../../../../reference/configuration/yb-master/#ysql-yb-enable-listen-notify) configuration).

After you enable the feature, the leader master creates internal objects (including the `yb_system` database and the `yb_system.pg_yb_notifications` table) in the background. If you run `LISTEN` or `NOTIFY` before those objects exist, you may see an error asking you to retry shortly; waiting a few seconds and retrying is expected during startup or right after turning the feature on.

## How notifications are delivered in YugabyteDB

In vanilla PostgreSQL, notifications are written through shared memory. In YugabyteDB, `NOTIFY` temporarily stores the notifications in the `yb_system.pg_yb_notifications` table. A per-node **notifications poller** reads that table using CDC-style logical replication (an internal logical replication slot on each tserver) and delivers the notifications to local listeners.

Because the poller relies on logical replication internally, LISTEN/NOTIFY also depends on the following tserver flags. These flags all have defaults that are compatible with LISTEN/NOTIFY, so no action is needed unless you have explicitly changed them.

| tserver flag | Default | Role for LISTEN/NOTIFY |
| :------------- | :------ | :---------------------- |
| `ysql_yb_enable_replication_commands` | `true` | Must be **true**, otherwise LISTEN fails. |
| `ysql_yb_enable_replication_slot_consumption` | `true` | Must be **true**, otherwise LISTEN fails. |
| `ysql_yb_allow_replication_slot_lsn_types` | `true` | Must be **true**, otherwise LISTEN fails. |
| `ysql_yb_allow_replication_slot_ordering_modes` | `false` | Must be **false**, otherwise LISTEN fails. |
| `max_replication_slots` | `10` | LISTEN/NOTIFY creates one internal replication slot per node. Ensure this limit is large enough to accommodate it alongside any user-created replication slots. |

Each tserver creates a **named logical replication slot** derived from the node identity (`yb_notifications_<tserver-uuid>`). Do not drop or repurpose that slot for other work.

## Differences from PostgreSQL

| Aspect | PostgreSQL | YugabyteDB |
| :----- | :--------- | :--------- |
| Sender identity | The notification message contains the sender's PID. A listener can compare this PID with its own backend PID to ignore self-notifications. | The notification message still carries a PID, but in a multi-node cluster different tservers can have backends with the same PID. To reliably identify the sender, include the tserver UUID in the notification payload. A session can obtain its tserver UUID from the `uuid` column of `yb_servers()`. |
| `pg_notification_queue_usage()` | Returns the usage fraction of the server's notification queue. | Returns the usage fraction of the **local tserver's** notification queue. |
| Behavior when the queue is full | New `NOTIFY` calls fail (return an error) until the queue drains. | The **slowest listener** on that tserver is terminated so the queue tail can advance. Applications must handle disconnections by reconnecting and re-issuing `LISTEN`. |

## Advanced tuning

These flags control how frequently the notifications poller checks for new notifications.

| tserver flag | Default | Description |
| :------------- | :------ | :---------- |
| `yb_notifications_poll_sleep_duration_nonempty_ms` | `1` | Wait time in milliseconds before the next poll when the previous poll returned data. |
| `yb_notifications_poll_sleep_duration_empty_ms` | `100` | Wait time in milliseconds before the next poll when the previous poll returned no data. |
