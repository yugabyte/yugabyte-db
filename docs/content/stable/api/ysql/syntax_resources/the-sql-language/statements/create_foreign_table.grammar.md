```output.ebnf
create_foreign_table ::= CREATE FOREIGN TABLE [ IF NOT EXISTS ] 
                         table_name ( [ foreign_table_elem [ , ... ] ] 
                         ) SERVER server_name 
                         [ OPTIONS ( fdw_options ) ]
```
