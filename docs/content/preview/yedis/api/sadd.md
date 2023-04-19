---
title: SADD
linkTitle: SADD
description: SADD
menu:
  preview:
    parent: api-yedis
    weight: 2250
aliases:
  - /preview/api/redis/sadd
  - /preview/api/yedis/sadd
type: docs
---
## Synopsis

**`SADD key value [value ...]`**

This command adds one or more given values to the set that is associated with the given `key`.

- If the `key` does not exist, a new set is created, and members are added with the given values.
- If the `key` is associated with a value that is not a set, an error is raised.
- If a specified `value` already exists in the given set, that `value` is ignored and not counted toward the total of newly added members.

## Return value

Depends on the configuration parameter `emulate_redis_responses`.

- If `emulate_redis_responses` is `true`, returns the number of new members that were added by this command not including the duplicates.
- If `emulate_redis_responses` is `false`, returns OK.

## Examples

`emulate_redis_responses` is `true`.

```sh
$ SADD yuga_world "Africa"
```

```
1
```

```sh
$ SADD yuga_world "America"
```

```
1
```

```sh
$ SMEMBERS yuga_world
```

```
1) "Africa"
2) "America"
```

`emulate_redis_responses` is `false`.

```sh
$ SADD yuga_world "Africa"
```

```
"OK"
```

```sh
$ SADD yuga_world "America"
```

```
"OK"
```

```sh
$ SMEMBERS yuga_world
```

```
1) "Africa"
2) "America"
```

## See also

[`scard`](../scard/), [`sismember`](../sismember/), [`smembers`](../smembers/), [`srem`](../srem/)
