---
title: SCARD
linkTitle: SCARD
description: SCARD
menu:
  preview:
    parent: api-yedis
    weight: 2260
aliases:
  - /preview/api/redis/scard
  - /preview/api/yedis/scard
type: docs
---

## Synopsis

**s**

This command finds the cardinality of the set that is associated with the given `key`. Cardinality is the number of elements in a set.

- If the `key` does not exist, 0 is returned.
- If the `key` is associated with a non-set value, an error is raised.

## Return value

Returns the cardinality of the set.

## Examples

You can do this as shown below.

```sh
$ SADD yuga_world "America"
```

```
1
```

```sh
$ SADD yuga_world "Asia"
```

```
1
```

```sh
$ SCARD yuga_world
```

```
2
```

## See also

[`sadd`](../sadd/), [`sismember`](../sismember/), [`smembers`](../smembers/), [`srem`](../srem/)
