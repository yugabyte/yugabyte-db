```output.ebnf
alter_database ::= ALTER DATABASE name 
                   [ [ WITH ] alter_database_option [ ... ]
                     | RENAME TO name
                     | OWNER TO { new_owner
                                  | CURRENT_USER
                                  | SESSION_USER }
                     | SET run_time_parameter { TO | = } 
                       { value | DEFAULT }
                     | SET run_time_parameter FROM CURRENT
                     | RESET run_time_parameter
                     | RESET ALL ]

alter_database_option ::= ALLOW_CONNECTIONS allowconn
                          | CONNECTION LIMIT connlimit
                          | IS_TEMPLATE istemplate
```
