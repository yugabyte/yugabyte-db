---
title: ECHO
linkTitle: ECHO
description: ECHO
menu:
  latest:
    parent: api-redis
    weight: 2050
aliases:
  - api/redis/echo
---

## SYNOPSIS
<b>`ECHO message`</b><br>
This command outputs the given `message`.

## RETURN VALUE
Returns the message.

## EXAMPLES
```{.sh .copy .separator-dollar}
$ ECHO "Yuga Yuga"
```
```sh
"Yuga Yuga"
```
