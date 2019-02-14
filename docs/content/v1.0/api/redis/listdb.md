---
title: LISTDB
linkTitle: LISTDB
description: LISTDB
menu:
  v1.0:
    parent: api-redis
    weight: 2036
---

## Synopsis

`LISTDB` is used to list all the yedis database(s) available for use. All databases other than the default database ("0") need to be created using the `CREATEDB` command before use.

A client can issue the `LISTDB` command through the redis-cli.

## Return Value
Returns an array of string values, with the yedis database names. 

## Examples

You can do this as shown below.

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
$ LISTDB
```

```
1) "0"
2) "second"
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

## See Also
[`createdb`](../createdb/)
[`listdb`](../listdb/)
[`deletedb`](../deletedb/)
[`flushdb`](../flushdb/)
[`flushall`](../flushall/)
[`select`](../select/)
