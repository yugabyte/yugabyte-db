---
title: ZCARD
linkTitle: ZCARD
description: ZCARD
menu:
  preview:
    parent: api-yedis
    weight: 2510
aliases:
  - /preview/api/redis/zcard
  - /preview/api/yedis/zcard
type: docs
---

## Synopsis

**`ZCARD key`**

This command returns the number of `members` in the sorted set at `key`. If `key` does not exist, 0 is returned.

- If `key` is associated with non sorted-set data, an error is returned.

## Return value

The cardinality of the sorted set.

## Examples

You can do this as shown below.

```sh
$ ZADD z_key 1.0 v1 2.0 v2
```

```
(integer) 2
```

```sh
$ ZADD z_key 3.0 v2
```

```
(integer) 0
```

```sh
$ ZCARD z_key
```

```
(integer) 2
```

```sh
$ ZCARD ts_key
```

```
(integer) 0
```

## See also

[`zadd`](../zadd/), [`zrange`](../zrange/), [`zrangebyscore`](../zrangebyscore/), [`zrem`](../zrem/), [`zrevrange`](../zrevrange)
