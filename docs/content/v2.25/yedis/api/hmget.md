---
title: HMGET
linkTitle: HMGET
description: HMGET
menu:
  v2.25:
    parent: api-yedis
    weight: 2160
aliases:
  - /stable/api/redis/hmget
  - /stable/api/yedis/hmget
type: docs
---

## Synopsis

**`HMGET key field [field ...]`**

This command fetches one or more values for the given fields of the hash that is associated with the given `key`.

- For every given `field`, (null) is returned if either `key` or `field` does not exist.
- If `key` is associated with a non-hash data, an error is raised.

## Return value

Returns list of string values of the fields in the same order that was requested.

## Examples

```sh
$ HMSET yugahash area1 "Africa" area2 "America"
```

```
"OK"
```

```sh
$ HMGET yugahash area1 area2 area_none
```

```
1) "Africa"
2) "America"
3) (null)
```

## See also

[`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hgetall`](../hgetall/), [`hkeys`](../hkeys/), [`hlen`](../hlen/), [`hmset`](../hmset/), [`hset`](../hset/), [`hincrby`](../hincrby/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)
