```output.ebnf
alter_foreign_data_wrapper ::= ALTER FOREIGN DATA WRAPPER fdw_name 
                               [ HANDLER handler_name | NO HANDLER ] 
                               [ VALIDATOR validator_name
                                 | NO VALIDATOR ] 
                               [ OPTIONS ( alter_fdw_options ) ] 
                               [ OWNER TO new_owner ] 
                               [ RENAME TO new_name ]
```
