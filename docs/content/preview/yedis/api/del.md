---
title: DEL
linkTitle: DEL
description: DEL
menu:
  preview:
    parent: api-yedis
    weight: 2040
aliases:
  - /preview/api/redis/del
  - /preview/api/yedis/del
type: docs
---

Single-key request only. Requests with multiple keys are not yet supported.

## Synopsis

**`DEL key`**

This command deletes the given `key`.

- If the `key` does not exist, it is ignored and not counted toward the total number of removed keys.

## Return value

Returns number of keys that were removed.

## Examples

```sh
$ SET yuga1 "America"
```

```
"OK"
```

```sh
$ SET yuga2 "Africa"
```

```
"OK"
```

```sh
$ DEL yuga1
```

```
1
```

```sh
$ DEL not_a_key
```

```
0
```

```sh
$ DEL yuga1 yuga2
```

```
"ERR del: Wrong number of arguments"
```

## See also

[`exists`](../exists/), [`flushall`](../flushall/), [`flushdb`](../flushdb/), [`get`](../get/), [`getrange`](../getrange/), [`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hset`](../hset/), [`hincrby`](../hincrby/), [`sadd`](../sadd/), [`set`](../set/)
