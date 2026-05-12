---
title: yb_cancel_transaction() function [YSQL]
headerTitle: yb_cancel_transaction()
linkTitle: yb_cancel_transaction()
description: Cancel a running distributed transaction by its transaction ID, from any node in the cluster.
menu:
  v2025.1_api:
    identifier: api-ysql-exprs-yb_cancel_transaction
    parent: api-ysql-exprs
    weight: 15
type: docs
---

## Synopsis

When running concurrent transactions in YugabyteDB, queries may become blocked waiting for locks held by other transactions. This can cause performance degradation and application timeouts. `yb_cancel_transaction()` is a function that allows you to cancel a running distributed transaction, from any node in the cluster. The function takes a transaction ID for input.

It is intended for superusers and DB admins to manually cancel problematic transactions, similar in spirit to PostgreSQL's backend-cancel functions. `yb_cancel_transaction()` is the preferred way to abort a blocking transaction, because it only needs the transaction ID and works cluster‑wide; you don't need to know which TServer the backend is on. (Unlike single-node PostgreSQL, the blocking query may be running on a different TServer than where you're connected.)

## Instructions

Basic usage:

```sql
SELECT yb_cancel_transaction('<yb_txn_id>');
```

Input is the YSQL transaction ID (UUID-style) as a string.

The function returns a boolean:

- true - the transaction was canceled.
- false - there was nothing to cancel (for example, the transaction was already completed).

## Example

To identify blocking transactions, use [pg_locks](../../../../explore/observability/pg-locks/).

For example, to find all blocked transactions, you can filter for locks that have not yet been granted:

```sql
SELECT * FROM pg_locks WHERE granted = false;
```

The output provides the blocking transaction UUID (`blocking_txn`), as well as the IP address of the TServer where the blocking query is running.

Take the `blocking_txn` value and cancel it from any node:

```sql
SELECT yb_cancel_transaction('<blocking_txn>');
```

## Learn more

- [pg_locks](../../../../explore/observability/pg-locks/)
- [Blocking queries](https://support.yugabyte.com/hc/en-us/articles/42907611477133-How-to-Check-Blocking-Queries-in-YugabyteDB#h_01KFMXKXPSAVKWY4PCD7BJV8AH)
