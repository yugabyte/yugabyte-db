---
title: FLUSHDB
linkTitle: FLUSHDB
description: FLUSHDB
menu:
  v1.0:
    parent: api-redis
    weight: 2065
---

## Synopsis
<b>`FLUSHDB`</b><br>
This command deletes all keys from a database.

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
$ FLUSHDB
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
[`del`](../del/), [`flushall`](../flushall/),[`deletedb`](../deletedb/)
