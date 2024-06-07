---
title: nextval() function [YSQL]
headerTitle: nextval()
linkTitle: nextval()
description: Returns the next value from the sequence cache for the current session.
menu:
  preview:
    identifier: api-ysql-exprs-nextval
    parent: api-ysql-exprs
aliases:
  - /preview/api/ysql/exprs/func_nextval
type: docs
---

## Synopsis

Use the `nextval( sequence_name )` function to return the next value from the sequence cache for the current session. If no more values are available in the cache, the session allocates a block of numbers for the cache and returns the first one. The number of elements allocated is determined by the `cache` option specified as part of the `CREATE SEQUENCE` statement.

## Semantics

### _sequence_name_

Specify the name of the sequence.

- An error is raised if a sequence reaches its minimum or maximum value.

## Caching values on the YB-TServer

If [ysql_sequence_cache_method](../../../../reference/configuration/yb-tserver/#ysql-sequence-cache-method) is set to `server`, sequence values are cached on the YB-TServer, to be shared with all connections on that YB-TServer. This is beneficial when many connections on the server are expected to get the next value of a sequence. Normally, each connection waits for replication to complete, which can be expensive, especially in a multi-region cluster. With the server cache method, only one connection waits for Raft replication and the rest retrieve values from the same cached range.

When the server cache method is used, the connection cache size is implicitly set to 1. When the cache method is changed from `connection` to `server`, sequences continue to use the connection cache until it is exhausted, at which point they begin using the server cache. When the cache method is changed from `server` to `connection`, sequences immediately begin using a connection cache. The server cache is not cleared in this case, and its values can later be retrieved if the cache method is again set to `server`.

**Limitations**

- Calling `setval` on a sequence or restarting a sequence is not currently compatible with server caching, as the cache will not be cleared. This issue is tracked in GitHub issue [#16225](https://github.com/yugabyte/yugabyte-db/issues/16225).
- Bidirectional xCluster replication and point-in-time-restore are not compatible with sequences and therefore are not compatible with this feature.

## Examples

### Create a basic sequence that increments by 1 every time nextval() is called

```plpgsql
yugabyte=# CREATE SEQUENCE s;
```

```output
CREATE SEQUENCE
```

Call nextval() a couple of times.

```plpgsql
yugabyte=# SELECT nextval('s');
```

```output
 nextval
---------
       1
(1 row)
```

```plpgsql
yugabyte=# SELECT nextval('s');
```

```output
 nextval
---------
       2
(1 row)
```

### Create a sequence with a cache of 3 values

```plpgsql
yugabyte=# CREATE SEQUENCE s2 CACHE 3;
```

```output
CREATE SEQUENCE
```

In the same session, call `nextval()`. The first time it's called, the session's cache will allocate numbers 1, 2, and 3. This means that the data for this sequence will have its `last_val` set to 3. This modification requires two RPC requests.

```plpgsql
SELECT nextval('s2');
```

```output
 nextval
---------
       1
(1 row)
```

The next call of `nextval()` in the same session will not generate new numbers for the sequence, so it is much faster than the first `nextval()` call because it will just use the next value available from the cache.

```plpgsql
SELECT nextval('s2');
```

```output
nextval
---------
       2
(1 row)
```

## See also

- [`CREATE SEQUENCE`](../../the-sql-language/statements/ddl_create_sequence)
- [`DROP SEQUENCE`](../../the-sql-language/statements/ddl_drop_sequence)
- [`currval()`](../func_currval)
- [`lastval()`](../func_lastval)
- [`setval()`](../func_setval)
