---
title: Advisory Locks in YugabyteDB
headerTitle: Advisory Locks in YugabyteDB
linkTitle: Advisory Locks
description: Advisory Locks implementation in YugabyteDB
headcontent: Understand how Advisory locks are implemented in YugabyteDB
menu:
  preview:
    identifier: advisory-locks
    parent: architecture
    weight: 10
type: docs
---
Advisory locks feature is in {{<tags/feature/tp>}} as of v2.25.1. 

## Overview

Advisory locks feature allows applications to manage concurrent access to resources through a cooperative locking mechanism. In PostgreSQL, if advisory lock is taken on one session, all sessions should be able to see the advisory locks acquired by any other session. Similarly, In YugabyteDB, if advisory lock is acquired on one session, all the sessions should be able to see the advisory locks regardless of the node the PG session is connected to. This is achieved by creating a system table pg_advisory_locks dedicated to host advisory locks. All advisory lock requests will be stored in that system table. Advisory locks YugabyteDB provide the same set of semantics as PostgreSQL. 
 
Advisory locks feature can be turned on using the [Advisory locks flags](../../reference/configuration/yb-tserver/#advisory-locks-flags).

### Types of advisory locks

**Session level**: Once acquired at session level, the advisory lock is held until it is explicitly released or the session ends. Unlike standard lock requests, session-level advisory lock requests do not honor transaction semantics: a lock acquired during a transaction that is later rolled back will still be held following the rollback, and likewise an unlock is effective even if the calling transaction fails later. A lock can be acquired multiple times by its owning process; for each completed lock request there must be a corresponding unlock request before the lock is actually released. 

Example: 
select pg_advisory_lock(10);

**Transaction level**:  Transaction-level lock requests, on the other hand, behave more like regular row level lock requests: they are automatically released at the end of the transaction, and there is no explicit unlock operation. This behavior is often more convenient than the session-level behavior for short-term usage of an advisory lock.

Example: 
select pg_advisory_xact_lock(10);

### Modes of acquiring advisory locks

**Exclusive Lock**: Only one session/transaction can hold the lock at a time. Other sessions/transactions can’t acquire the lock until the lock is released.

Example: 
select pg_advisory_lock(10); 
select pg_advisory_xact_lock(10);

**Shared Lock**: Multiple sessions/transactions can hold the lock simultaneously. However, no session/transaction can acquire an exclusive lock while shared locks are held.

Example: 
select pg_advisory_lock_shared(10); 
select pg_advisory_xact_lock_shared(10);

### Ways to acquire advisory locks

**Blocking**: With the blocking way, the process trying to acquire the lock will wait till the lock is acquired. 
**Non-blocking**: With non-blocking way, the process will immediately return with a boolean value stating if the lock is acquired or not.

Example: 
select pg_try_advisory_lock(10);

