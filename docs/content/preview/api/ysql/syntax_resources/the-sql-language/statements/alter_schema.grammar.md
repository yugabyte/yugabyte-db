```output.ebnf
alter_schema ::= ALTER SCHEMA schema_name 
                 { RENAME TO new_name
                   | OWNER TO { new_owner
                                | CURRENT_USER
                                | SESSION_USER } }
```
