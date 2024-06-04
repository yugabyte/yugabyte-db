---
title: GET
linkTitle: GET
description: GET
menu:
  preview:
    parent: api-yedis
    weight: 2070
aliases:
  - /preview/api/redis/get
  - /preview/api/yedis/get
type: docs
---

## Synopsis

**`GET key`**

This command fetches the value that is associated with the given `key`.

- If the `key` does not exist, null is returned.
- If the `key` is associated with a non-string value, an error is raised.

## Return value

Returns string value of the given `key`.

## Examples

```sh
$ GET yugakey
```

```
(null)
```

```sh
$ SET yugakey "Yugabyte"
```

```
"OK"
```

```sh
$ GET yugakey
```

```
"Yugabyte"
```

## See also

[`append`](../append/), [`getrange`](../getrange/), [`getset`](../getset/), [`incr`](../incr/), [`incrby`](../incrby/), [`set`](../set/), [`setrange`](../setrange/), [`strlen`](../strlen/)
