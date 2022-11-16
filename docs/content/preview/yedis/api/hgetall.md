---
title: HGETALL
linkTitle: HGETALL
description: HGETALL
menu:
  preview:
    parent: api-yedis
    weight: 2130
aliases:
  - /preview/api/redis/hgetall
  - /preview/api/yedis/hgetall
type: docs
---

## Synopsis

**`HGETALL key`**

This command fetches the full content of all fields and all values of the hash that is associated with the given `key`.

- If the given `key` does not exist, and empty list is returned.
- If the given `key` is associated with non-hash data, an error is raised.

## Return value

Returns list of fields and values.

## Examples

You can do this as shown below.

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
$ HGETALL yugahash
```

```
1) area1
2) "Africa"
3) area2
4) "America"
```

## See also

[`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hkeys`](../hkeys/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hset`](../hset/), [`hincrby`](../hincrby/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)
