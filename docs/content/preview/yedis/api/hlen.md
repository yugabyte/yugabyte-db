---
title: HLEN
linkTitle: HLEN
description: HLEN
menu:
  preview:
    parent: api-yedis
    weight: 2150
aliases:
  - /preview/api/redis/hlen
  - /preview/api/yedis/hlen
type: docs
---

## Synopsis

**`HLEN key`**

This command fetches the number of fields in the hash that is associated with the given `key`.

- If the `key` does not exist, 0 is returned.
- If the `key` is associated with non-hash data, an error is raised.

## Return value

Returns number of fields in the specified hash.

## Examples

```sh
$ HSET yugahash area1 "Africa"
```

```
1
```

```sh
$ HSET yugahash area2 "America"
```

```
1
```

```sh
$ HLEN yugahash
```

```
2
```

## See also

[`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hgetall`](../hgetall/), [`hkeys`](../hkeys/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hset`](../hset/), [`hincrby`](../hincrby/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)
