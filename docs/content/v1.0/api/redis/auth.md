---
title: AUTH
linkTitle: AUTH
description: AUTH
menu:
  v1.0:
    parent: api-redis
    weight: 2020
---
`AUTH` authenticates a client connection to YugaByte YEDIS API.

## Synopsis
<b>`AUTH password`</b><br>
This command authenticates a client connection to YugaByte DB's YEDIS API.

YugaByte DB can be setup to require a password for clients to authenticate themselves. The password verification can be enforced by using the `CONFIG` command to set the intended password (See `CONFIG`).

YEDIS allows for multiple passwords (up to 2) to be accepted.
<li>If the given `password` matches with any of the server configured password(s), server returns the status string "OK" and begins processing commands from the authenticated client.</li>
<li>If the given `password` does not match with any of the server configured password(s), an error is raised</li>

## Return Value
Returns a status string if the password is accepted. Returns an error if the password is rejected.

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
[`config`](../config/)
