---
title: DELETEDB
linkTitle: DELETEDB
description: DELETEDB
menu:
  preview:
    parent: api-yedis
    weight: 2034
aliases:
  - /preview/api/yedis/deletedb
type: docs
---

## Synopsis

`DELETEDB` is used to delete a yedis database that is no longer needed.

A client can issue the `DELETEDB` command through the redis-cli.

## Return value

Returns a status string upon success.

## Examples

```sh
$ LISTDB
```

```
1) "0"
```

```sh
$ CREATEDB "second"
```

```
"OK"
```

```sh
$ CREATEDB "3.0"
```

```
"OK"
```

```sh
$ LISTDB
```

```
1) "0"
2) "3.0"
3) "second"
```

```sh
$ DELETEDB "3.0"
```

```
"OK"
```

```sh
$ LISTDB
```

```
1) "0"
2) "second"
```

## See also

[`createdb`](../createdb/)
[`listdb`](../listdb/)
[`deletedb`](../deletedb/)
[`flushdb`](../flushdb/)
[`flushall`](../flushall/)
[`select`](../select/)
