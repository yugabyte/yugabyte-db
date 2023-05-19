```output.ebnf
create_matview ::= CREATE MATERIALIZED VIEW [ IF NOT EXISTS ]  
                   matview_name [ ( column_name [ , ... ] ) ]  
                   [ WITH ( storage_parameters ) ]  
                   [ TABLESPACE tablespace ]  AS query 
                   [ WITH [ NO ] DATA ]
```
