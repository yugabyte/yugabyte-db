---
title: STRLEN
linkTitle: STRLEN
description: STRLEN
menu:
  preview:
    parent: api-yedis
    weight: 2320
aliases:
  - /preview/api/redis/strlen
  - /preview/api/yedis/strlen
type: docs
---

## Synopsis

**`STRLEN key`**

This command finds the length of the string value that is associated with the given `key`.

-  If `key` is associated with a non-string value, an error is raised.
-  If `key` does not exist, 0 is returned.

## Return value

Returns length of the specified string.

## Examples

```sh
$ SET yugakey "string value"
```

```
"OK"
```

```sh
$ STRLEN yugakey
```

```
12
```

```sh
$ STRLEN undefined_key
```

```
0
```

## See also

[`append`](../append/), [`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`incr`](../incr/), [`incrby`](../incrby/), [`set`](../set/), [`setrange`](../setrange/)
