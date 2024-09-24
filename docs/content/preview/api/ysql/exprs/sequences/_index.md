---
title: Functions related to sequences
headerTitle: Functions related to sequences
linkTitle: Sequence functions
description: Functions operting on sequences
image: /images/section_icons/api/subsection.png
menu:
  preview:
    identifier: api-ysql-sequences
    parent: api-ysql-exprs
    weight: 70
type: indexpage
---

YSQL supports all PostgreSQL-compatible built-in functions and operators. The following are the currently documented ones.

| Statement | Description |
|-----------|-------------|
| [nextval()](func_nextval) | Returns the next value for the specified sequence in the current session |
| [currval()](func_currval) | Returns the value returned by the most recent call to _nextval()_ for the specified sequence in the current session |
| [setval()](func_setval) | Set and return the value for for _any_ sequence|
| [lastval()](func_lastval) | Returns the value returned by the most recent call to _nextval()_ for _any_ sequence|