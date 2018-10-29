---
title: CONFIG
linkTitle: CONFIG
description: CONFIG
menu:
  v1.0:
    parent: api-redis
    weight: 2030
---

## Synopsis
Early Releases: Not fully supported. YEDIS only supports the <code>CONFIG</code> command to set the required password(s) for client authentication. All other <code>CONFIG</code> requests would be accepted as valid command without further processing.

To enable authentication, one can set a password that would be required for connections to communicate with the redis server. This is done using the following command:
<b>`CONFIG SET requirepass password[,password2]`</b><br>

## Return Value
Returns a status string.

## Examples
```{.sh .copy .separator-dollar}
$ CONFIG SET requirepass "yugapass"
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ AUTH "yugapass"
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ AUTH "bad"
```
```sh
"ERR: Bad Password."
```
```{.sh .copy .separator-dollar}
$ CONFIG SET requirepass "yugapassA,yugapassB"
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ AUTH "yugapassA"
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ AUTH "yugapassB"
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ AUTH "yugapassC"
```
```sh
"ERR: Bad Password."
```

## See Also
[`auth`](../auth/)
