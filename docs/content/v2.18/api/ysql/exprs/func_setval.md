---
title: setval() function [YSQL]
headerTitle: setval()
linkTitle: setval()
description: Set and return the value for the specified sequence.
menu:
  v2.18:
    identifier: api-ysql-exprs-setval
    parent: api-ysql-exprs
type: docs
---


## Synopsis

Use the `setval(sequence_name, value, is_called)` function to set and return the value for the specified sequence.
`UPDATE` privilege on the sequence is required to call this function.

Calling the function with two parameters defaults `is_called` to `true`, meaning that the `nextval` will advance the sequence prior to returning the value, and `currval` will also return the specified value.

When called with `is_called` set to `false`, `nextval` will return the specified value and the value reported by `currval` will not be changed.

`setval()` returns just the value of its second argument.

## Semantics

### _sequence_name_

Specify the name of the sequence.

### _value_

Specify the value of the sequence.

### _is_called_

Set `is_called` to `true` or `false`.

## Examples

### Create a sequence

```plpgsql
yugabyte=# CREATE SEQUENCE s;
```

```
CREATE SEQUENCE
```

Use `setval` with `is_called` set to `true`:
```sql
yugabyte=# SELECT setval('s', 21);
yugabyte=# SELECT setval('s', 21, true);  -- the same command as above
yugabyte=# SELECT nextval('s');
```

```output
 nextval
---------
      22
(1 row)
```

Use `setval` with `is_called` set to `false`:
```sql
yugabyte=# SELECT setval('s', 21, false);
yugabyte=# SELECT nextval('s');
```

```output
 nextval
---------
      21
(1 row)
```


{{< note title="Note" >}}
`setval` changes are immediately visible in other transactions and are not rolled back if the transaction is rolled back.
{{< /note >}}


## See also

- [`CREATE SEQUENCE`](../../the-sql-language/statements/ddl_create_sequence)
- [`DROP SEQUENCE`](../../the-sql-language/statements/ddl_drop_sequence)
- [`currval()`](../func_currval)
- [`nextval()`](../func_nextval)
- [`lastval()`](../func_lastval)
