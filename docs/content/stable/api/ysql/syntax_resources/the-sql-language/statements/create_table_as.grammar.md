```output.ebnf
create_table_as ::= CREATE [ TEMPORARY | TEMP ] TABLE 
                    [ IF NOT EXISTS ]  table_name 
                    [ ( column_name [ , ... ] ) ]  AS subquery 
                    [ WITH [ NO ] DATA ]
```
