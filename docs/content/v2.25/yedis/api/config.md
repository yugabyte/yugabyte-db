---
title: CONFIG
linkTitle: CONFIG
description: CONFIG
menu:
  preview:
    parent: api-yedis
    weight: 2030
type: docs
---

## Synopsis

YEDIS only supports the <code>CONFIG</code> command to set the required password(s) for client authentication. All other <code>CONFIG</code> requests would be accepted as valid command without further processing.

To enable authentication, one can set a password that would be required for connections to communicate with the redis server. This is done using the following command:

**`CONFIG SET requirepass password[,password2]`**

## Return value

Returns a status string.

## Examples

```sh
$ CONFIG SET requirepass "yugapass"
```

```
"OK"
```

```sh
$ AUTH "yugapass"
```

```
"OK"
```

```sh
$ AUTH "bad"
```

```
"ERR: Bad Password."
```

```sh
$ CONFIG SET requirepass "yugapassA,yugapassB"
```

```
"OK"
```

```sh
$ AUTH "yugapassA"
```

```
"OK"
```

```sh
$ AUTH "yugapassB"
```

```
"OK"
```

```sh
$ AUTH "yugapassC"
```

```
"ERR: Bad Password."
```

## See also

[`auth`](../auth/)
