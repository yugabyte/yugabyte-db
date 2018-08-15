---
title: ECHO
linkTitle: ECHO
description: ECHO
menu:
  1.1-beta:
    parent: api-redis
    weight: 2050
aliases:
  - api/redis/echo
  - api/yedis/del
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
