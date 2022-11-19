---
title: SISMEMBER
linkTitle: SISMEMBER
description: SISMEMBER
menu:
  preview:
    parent: api-yedis
    weight: 2290
aliases:
  - /preview/api/redis/sismember
  - /preview/api/yedis/sismember
type: docs
---

## Synopsis

**`SISMEMBER key member_value`**

This command is a predicate for whether or not a value is a member of a set that is associated with the given  `key`.

- If the `key` is associated with a value that is not a set, an error is raised.
- If the `key` does not exist, its associated set is empty, and the command returns 0.
- If the `member` belongs to the given set, an integer of 1 is returned.

## Return Value

Returns 1 if the specified member exists. Returns 0 otherwise.

## Examples

```sh
$ SADD yuga_world "America"
```

```
1
```

```sh
$ SISMEMBER yuga_world "America"
```

```
1
```

```sh
$ SISMEMBER yuga_world "Moon"
```

```
0
```

## See also

[`sadd`](../sadd/), [`scard`](../scard/), [`smembers`](../smembers/), [`srem`](../srem/)
