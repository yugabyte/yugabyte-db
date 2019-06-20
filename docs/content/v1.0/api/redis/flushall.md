---
title: FLUSHALL
linkTitle: FLUSHALL
description: FLUSHALL
menu:
  v1.0:
    parent: api-redis
    weight: 2064
---

## Synopsis
<b>`FLUSHALL`</b><br>
This command deletes all keys from all databases.

## Return Value
Returns status string.

## Examples

You can do this as shown below.

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

## See Also
[`del`](../del/), [`flushdb`](../flushdb/)
