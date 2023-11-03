---
title: ZREM
linkTitle: ZREM
description: ZREM
menu:
  preview:
    parent: api-yedis
    weight: 2530
aliases:
  - /preview/api/redis/zrem
  - /preview/api/yedis/zrem
type: docs
---

## Synopsis

`ZREM key member [member...]`

This command removes the `members` specified in the sorted set at `key` and returns the number of `members` removed.
`Members` specified that do not exist in key are ignored. If `key` does not exist, 0 is returned.

- If `key` is associated with non sorted-set data, an error is returned.

## Return Value

The number of `members` removed from the sorted set.

## Examples

You can do this as shown below.

```sh
$ ZADD z_key 1.0 v1 2.0 v2
```

```
(integer) 2
```

```sh
$ ZREM z_key v2 v3
```

```
(integer) 1
```

```sh
$ ZREM z_key v1 v2 v3
```

```
(integer) 1
```

```sh
$ ZCARD z_key
```

```
(integer) 0
```

## See also

[`zadd`](../zadd/), [`zcard`](../zcard/), [`zrange`](../zrange/), [`zrangebyscore`](../zrangebyscore/), [`zrevrange`](../zrevrange)
