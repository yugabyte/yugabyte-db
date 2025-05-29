---
title: FLUSHALL
linkTitle: FLUSHALL
description: FLUSHALL
menu:
  2025.1:
    parent: api-yedis
    weight: 2064
type: docs
---

## Synopsis

**`FLUSHALL`**

This command deletes all keys from all databases.

This functionality can be disabled by setting the yb-tserver `--yedis_enable_flush` flag to `false`.

## Return value

Returns status string.

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
$ GET yuga1
```

```
"America"
```

```sh
$ GET yuga2
```

```
"Africa"
```

```sh
$ FLUSHALL
```

```
"OK"
```

```sh
$ GET yuga1
```

```
(null)
```

```sh
$ GET yuga2
```

```
(null)
```

## See also

[`del`](../del/), [`flushdb`](../flushdb/)
