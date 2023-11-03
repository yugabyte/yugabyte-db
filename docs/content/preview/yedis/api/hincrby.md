---
title: HINCRBY
linkTitle: HINCRBY
description: HINCRBY
menu:
  preview:
    parent: api-yedis
    weight: 2135
aliases:
  - /preview/api/redis/hincrby
  - /preview/api/yedis/hincrby
type: docs
---

## Synopsis

**`HINCRBY key field delta`**

This command adds `delta` to the number that is associated with the given field `field` for the hash `key`. The numeric value must a 64-bit signed integer.

- If the `key` does not exist, a new hash container is created. If the field `field` does not exist in the hash container, the associated string is set to "0".
- If the given `key` is not associated with a hash type, or if the string  associated with `field` cannot be converted to an integer, an error is raised.

## Return value

Returns the value after addition.

## Examples

```sh
$ HSET yugahash f1 5
```

```
1
```

```sh
$ HINCRBY yugahash f1 3
```

```
8
```

```sh
$ HINCRBY yugahash non-existent-f2 4
```

```
4
```

```sh
$ HINCRBY non-existent-yugahash f1 3
```

```
3
```

## See also

[`hexists`](../hexists/), [`hget`](../hget/), [`hgetall`](../hgetall/), [`hkeys`](../hkeys/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hset`](../hset/), [`hincrby`](../hincrby/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)
