---
title: HKEYS
linkTitle: HKEYS
description: HKEYS
menu:
  2025.1:
    parent: api-yedis
    weight: 2140
type: docs
---

## Synopsis

**`HKEYS key`**

This command fetches all fields of the hash that is associated with the given `key`.

- If the `key` does not exist, an empty list is returned.
- If the `key` is associated with non-hash data, an error is raised.

## Return value

Returns list of fields in the specified hash.

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
$ HKEYS yugahash
```

```
1) "area1"
2) "area2"
```

## See also

[`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hgetall`](../hgetall/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hset`](../hset/), [`hincrby`](../hincrby/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)
