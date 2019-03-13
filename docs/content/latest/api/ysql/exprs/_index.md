---
title: Expressions
description: Expressions
summary: Expressions
image: /images/section_icons/api/ysql.png
menu:
  latest:
    identifier: api-ysql-exprs
    parent: api-ysql
    weight: 4300
aliases:
  - /latest/api/ysql/exprs/
isTocNested: true
showAsideToc: true
---

YSQL supports all PostgreSQL-compatible builtin functions and operators. Following table lists the currently documented ones.

| Statement | Description |
|-----------|-------------|
| [`func_currval`](func_currval) | Returns the last value returned by `nextval()` for the specified sequence in the current session |
| [`func_lastval`](func_lastval) | Returns the value returned from the last call to `nextval()` (for any sequence) in the current session|
| [`func_nextval`](func_nextval) | Returns the next value from the session's sequence cache |
