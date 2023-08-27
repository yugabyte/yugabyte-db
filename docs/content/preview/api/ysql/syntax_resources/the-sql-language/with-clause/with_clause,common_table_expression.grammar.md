```output.ebnf
with_clause ::= WITH [ RECURSIVE ] 
                { common_table_expression [ , ... ] }

common_table_expression ::= cte_name [ ( column_name [ , ... ] ) ] AS 
                            ( { select
                                | values
                                | insert
                                | update
                                | delete } )
```
