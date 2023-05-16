```output.ebnf
lock_table ::= LOCK [ TABLE ] { table_expr [ , ... ] } 
               [ IN lockmode MODE ] [ NOWAIT ]

lockmode ::= ACCESS SHARE
             | ROW SHARE
             | ROW EXCLUSIVE
             | SHARE UPDATE EXCLUSIVE
             | SHARE
             | SHARE ROW EXCLUSIVE
             | EXCLUSIVE
             | ACCESS EXCLUSIVE
```
