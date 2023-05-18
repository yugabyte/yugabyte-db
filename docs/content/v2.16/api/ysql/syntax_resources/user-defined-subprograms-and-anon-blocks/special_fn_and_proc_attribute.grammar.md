```output.ebnf
special_fn_and_proc_attribute ::= RENAME TO subprogram_name
                                  | OWNER TO 
                                    { role_name
                                      | CURRENT_ROLE
                                      | CURRENT_USER
                                      | SESSION_USER }
                                  | SET SCHEMA schema_name
                                  | [ NO ] DEPENDS ON EXTENSION 
                                    extension_name
```
