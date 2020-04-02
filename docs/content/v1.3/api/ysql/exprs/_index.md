---
title: Expressions
description: Expressions
summary: Expressions
image: /images/section_icons/api/ysql.png
block_indexing: true
menu:
  v1.3:
    identifier: api-ysql-exprs
    parent: api-ysql
    weight: 4300
isTocNested: true
showAsideToc: true
---

Yugabyte Structured Query Language (YSQL) supports all PostgreSQL-compatible built-in functions and operators. The following statements are the currently documented ones.

| Statement | Description |
|-----------|-------------|
| [`func_currval`](func_currval) | Returns the last value returned by `nextval()` for the specified sequence in the current session |
| [`func_lastval`](func_lastval) | Returns the value returned from the last call to `nextval()` (for any sequence) in the current session|
| [`func_nextval`](func_nextval) | Returns the next value from the session's sequence cache |
