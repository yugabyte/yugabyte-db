```output.ebnf
with_clause ::= [ WITH [ RECURSIVE ] 
                  { common_table_expression [ , ... ] } ]

common_table_expression ::= name [ ( name [ , ... ] ) ] AS ( 
                            { select
                              | values
                              | insert
                              | update
                              | delete } )
```
