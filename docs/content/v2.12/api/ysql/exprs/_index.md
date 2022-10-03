---
title: Functions and operators [YSQL]
headerTitle: Functions and operators
linkTitle: Functions and operators
description: YSQL supports all PostgreSQL-compatible built-in functions and operators.
image: /images/section_icons/api/ysql.png
menu:
  v2.12:
    identifier: api-ysql-exprs
    parent: api-ysql
    weight: 4300
type: indexpage
---

YSQL supports all PostgreSQL-compatible built-in functions and operators. The following are the currently documented ones.

| Statement | Description |
|-----------|-------------|
| [`currval`](func_currval) | Returns the last value returned by `nextval()` for the specified sequence in the current session |
| [`lastval`](func_lastval) | Returns the value returned from the last call to `nextval()` (for any sequence) in the current session|
| [`nextval`](func_nextval) | Returns the next value from the session's sequence cache |
| [`yb_hash_code`](func_yb_hash_code) | Returns the partition hash code for a given set of expressions |
| [`yb_is_local_table`](func_yb_is_local_table) | Returns whether the given 'oid' is a table replicated only in the local region |
| [`JSON functions and operators`](../datatypes/type_json/functions-operators/) | Detailed list of JSON-specific functions and operators |
| [`Array functions and operators`](../datatypes/type_array/functions-operators/) | Detailed list of array-specific functions and operators |
| [`Aggregate functions`](./aggregate_functions/) | Detailed list of YSQL aggregate functions |
| [`Window functions`](./window_functions/) | Detailed list of YSQL window functions |
