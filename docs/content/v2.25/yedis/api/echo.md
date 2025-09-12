---
title: ECHO
linkTitle: ECHO
description: ECHO
menu:
  preview:
    parent: api-yedis
    weight: 2050
aliases:
  - /preview/api/redis/echo
  - /preview/api/yedis/del
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
