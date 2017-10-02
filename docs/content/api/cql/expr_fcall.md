---
title: Builtin Function Call
summary: Combination of one or more values.
toc: false
---

## Synopsis
Function call expression applies the specified function to to given arguments between parentheses and return the result of the computation.

## Syntax
```
function_call ::= function_name '(' [ arguments ... ] ')'
```

## Builtin Functions

| Function | Return Type | Argument Type | Description |
|----------|-------------|---------------|-------------|
| BlobAs\<Type> | \<Type> | (`BLOB`) | Converts a value from `BLOB` |
| \<Type>AsBlob | `BLOB` | (\<Type>) | Converts a value to `BLOB` |
| DateOf | `TIMESTAMP` | (`TIMEUUID`) | Conversion |
| MaxTimeUuid | `TIMEUUID` | (`TIMESTAMP`) | Returns the associated max time uuid  |
| MinTimeUuid | `TIMEUUID` | (`TIMESTAMP`) | Returns the associated min time uuid  |
| Now | `TIMEUUID` | () | Returns the UUID of the current timestamp |
| TTL | `BIGINT` | (<AnyType>) | Seek time-to-live of a column |
| ToDate | `DATE` | (`TIMESTAMP`) | Conversion |
| ToDate | `DATE` | (`TIMEUUID`) | Converts `TIMEUUID` to `DATE` |
| ToTime | `TIME` | (`TIMESTAMP`) | Conversion |
| ToTime | `TIME` | (`TIMEUUID`) | Conversion |
| ToTimestamp | `TIMESTAMP` | (`DATE`) | Conversion |
| ToTimestamp | `TIMESTAMP` | (`TIMEUUID`) | Conversion |
| ToUnixTimestamp | `BIGINT` | (`DATE`) | Conversion |
| ToUnixTimestamp | `BIGINT` | (`TIMESTAMP`) | Conversion |
| ToUnixTimestamp | `BIGINT` | (`TIMEUUID`) | Conversion |
| UnixtimestampOf | `BIGINT` | (`TIMEUUID`) | Conversion |
| WriteTime | `BIGINT` | (<AnyType>) | Returns the time when the column was written |

## Aggregate Functions

| Function | Description |
|----------|-------------|
| COUNT | Returns number of selected rows |
| SUM | Returns sums of column values |
| AVG | Returns the average of column values |
| MIN | Returns the minimum value of column values |
| MAX | Returns the maximum value of column values |

## Semantics

<li>The argument datatypes must be convertible to the expected type for that argument that was specified by the function definition.</li>
<li>Function execution will return a value of the specified type by the function definition.</li>
<li>YugaByte allows function calls to be used any where that expression is allowed.</li>

## See Also
[All Expressions](..##expressions)
