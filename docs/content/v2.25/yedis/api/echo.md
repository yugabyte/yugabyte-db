---
title: ECHO
linkTitle: ECHO
description: ECHO
menu:
  v2.25:
    parent: api-yedis
    weight: 2050
aliases:
  - /stable/api/redis/echo
  - /stable/api/yedis/del
type: docs
---

## Synopsis

**`ECHO message`**

This command outputs the given `message`.

## Return Value

Returns the message.

## Examples

```sh
$ ECHO "Yuga Yuga"
```

```output
"Yuga Yuga"
```
