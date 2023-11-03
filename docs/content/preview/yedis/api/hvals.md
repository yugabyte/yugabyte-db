---
title: HVALS
linkTitle: HVALS
description: HVALS
menu:
  preview:
    parent: api-yedis
    weight: 2200
aliases:
  - /preview/api/redis/hvals
  - /preview/api/yedis/hvals
type: docs
---

## Synopsis

**`HVALS key`**

This command selects all the values in the hash that is associated with the given `key`.

- If the `key` does not exist, an empty list is returned.
- if the `key` is associated with a non-hash data, an error is raised.

## Return value

Returns list of values in the specified hash.

## Examples

```sh
$ HMSET yugahash area1 "America" area2 "Africa"
```

```
"OK"
```

```sh
$ HVALS yugahash
```

```
1) "America"
2) "Africa"
```

## See also

[`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hgetall`](../hgetall/), [`hkeys`](../hkeys/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hset`](../hset/), [`hincrby`](../hincrby/), [`hstrlen`](../hstrlen/)
