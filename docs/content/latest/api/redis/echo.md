---
title: ECHO
linkTitle: ECHO
description: ECHO
menu:
  latest:
    parent: api-redis
    weight: 2050
aliases:
  - /latest/api/redis/echo
  - /latest/api/yedis/del
isTocNested: true
showAsideToc: true
---

## Synopsis
<b>`ECHO message`</b><br>
This command outputs the given `message`.

## Return Value
Returns the message.

## Examples
```{.sh .copy .separator-dollar}
$ ECHO "Yuga Yuga"
```
```sh
"Yuga Yuga"
```
