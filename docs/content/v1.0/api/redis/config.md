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

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ CONFIG SET requirepass "yugapass"
```
</div>
```sh
"OK"
```
<div class='copy separator-dollar'>
```sh
$ AUTH "yugapass"
```
</div>
```sh
"OK"
```
<div class='copy separator-dollar'>
```sh
$ AUTH "bad"
```
</div>
```sh
"ERR: Bad Password."
```
<div class='copy separator-dollar'>
```sh
$ CONFIG SET requirepass "yugapassA,yugapassB"
```
</div>
```sh
"OK"
```
<div class='copy separator-dollar'>
```sh
$ AUTH "yugapassA"
```
</div>
```sh
"OK"
```
<div class='copy separator-dollar'>
```sh
$ AUTH "yugapassB"
```
</div>
```sh
"OK"
```
<div class='copy separator-dollar'>
```sh
$ AUTH "yugapassC"
```
</div>
```sh
"ERR: Bad Password."
```

## See Also
[`auth`](../auth/)
