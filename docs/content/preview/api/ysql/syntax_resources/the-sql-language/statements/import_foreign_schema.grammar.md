```output.ebnf
import_foreign_schema ::= IMPORT FOREIGN SCHEMA remote_schema  
                          [ { LIMIT TO | EXCEPT } ( table_name [ ... ] 
                            ) ]  FROM SERVER server_name INTO 
                          local_schema  [ OPTIONS ( fdw_options ) ]
```
