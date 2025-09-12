---
title: EXISTS
linkTitle: EXISTS
description: EXISTS
menu:
  preview:
    parent: api-yedis
    weight: 2060
aliases:
  - /preview/api/redis/exist
  - /preview/api/yedis/exist
type: docs
---
Single-key request only. Requests with multiple keys are not yet supported.

## Synopsis

**`EXISTS key [key ...]`**

This command is a predicate to check whether or not the given `key` exists.

## Return value

Returns the number of existing keys.

## Examples

```sh
$ SET yuga1 "Africa"
```

```
"OK"
```

```sh
$ SET yuga2 "America"
```

```
"OK"
```

```sh
$ EXISTS yuga1
```

```
1
```

```sh
$ EXISTS yuga1 yuga2 not_a_key
```

```
2
```

## See also

[`del`](../del/), [`get`](../get/), [`getrange`](../getrange/), [`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hset`](../hset/), [`hincrby`](../hincrby/), [`sadd`](../sadd/), [`set`](../set/)
