---
title: HGET
linkTitle: HGET
description: HGET
menu:
  preview:
    parent: api-yedis
    weight: 2120
aliases:
  - /preview/api/redis/hget
  - /preview/api/yedis/hget
type: docs
---

## Synopsis

**`HGET key field`**

This command fetches the value for the given `field` in the hash that is specified by the given `key`.

- If the given `key` or `field` does not exist, nil is returned.
- If the given `key` is associated with non-hash data, an error is raised.

## Return value

Returns the value for the given `field`

## Examples

```sh
$ HSET yugahash area1 "America"
```

```
1
```

```sh
$ HGET yugahash area1
```

```
"America"
```

## See also

[`hdel`](../hdel/), [`hexists`](../hexists/), [`hgetall`](../hgetall/), [`hkeys`](../hkeys/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hset`](../hset/), [`hincrby`](../hincrby/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)
