---
title: HMSET
linkTitle: HMSET
description: HMSET
menu:
  preview:
    parent: api-yedis
    weight: 2170
aliases:
  - /preview/api/redis/hmset
  - /preview/api/yedis/hmset
type: docs
---

## Synopsis

**`HMSET key field value [field value ...]`**

This command sets the data for the given `field` with the given `value` in the hash that is specified by `key`.

- If the given `field` already exists in the specified hash, this command overwrites the existing value with the given `value`.
- If the given `key` does not exist, a new hash is created for the `key`, and the given values are inserted to the associated given fields.
- If the given `key` is associated with a non-hash data, an error is raised.

## Return value

Returns status string.

## Examples

```sh
$ HMSET yugahash area1 "America" area2 "Africa"
```

```
"OK"
```

```sh
$ HGET yugahash area1
```

```
"America"
```

## See also

[`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hgetall`](../hgetall/), [`hkeys`](../hkeys/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hset`](../hset/), [`hincrby`](../hincrby/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)
