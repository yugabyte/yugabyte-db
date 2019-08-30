---
title: EXISTS
linkTitle: EXISTS
description: EXISTS
menu:
  v1.0:
    parent: api-redis
    weight: 2060
---
Early Releases: Single-key request only. Requests with multiple keys are not yet supported.

## Synopsis
<b>`EXISTS key [key ...]`</b><br>
This command is a predicate to check whether or not the given `key` exists.

## Return Value
Returns the number of existing keys.

## Examples

You can do this as shown below.

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

## See Also
[`del`](../del/), [`get`](../get/), [`getrange`](../getrange/), [`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hset`](../hset/), [`hincrby`](../hincrby/), [`sadd`](../sadd/), [`set`](../set/)
