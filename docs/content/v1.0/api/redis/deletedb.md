---
title: DELETEDB
linkTitle: DELETEDB
description: DELETEDB
menu:
  v1.0:
    parent: api-redis
    weight: 2034
---

## Synopsis

`DELETEDB` is used to delete a yedis database that is no longer needed.

A client can issue the `DELETEDB` command through the redis-cli.

## Return Value
Returns a status string upon success.

## Examples

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ LISTDB
```
</div>
```sh
1) "0"
```
<div class='copy separator-dollar'>
```sh
$ CREATEDB "second"
```
</div>
```sh
"OK"
```
<div class='copy separator-dollar'>
```sh
$ CREATEDB "3.0"
```
</div>
```sh
"OK"
```
<div class='copy separator-dollar'>
```sh
$ LISTDB
```
</div>
```sh
1) "0"
2) "3.0"
3) "second"
```
<div class='copy separator-dollar'>
```sh
$ DELETEDB "3.0"
```
</div>
```sh
"OK"
```
<div class='copy separator-dollar'>
```sh
$ LISTDB
```
</div>
```sh
1) "0"
2) "second"
```

## See Also
[`createdb`](../createdb/)
[`listdb`](../listdb/)
[`deletedb`](../deletedb/)
[`flushdb`](../flushdb/)
[`flushall`](../flushall/)
[`select`](../select/)
