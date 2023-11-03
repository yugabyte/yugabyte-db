```output.ebnf
alter_procedure ::= ALTER PROCEDURE subprogram_name ( 
                    [ subprogram_signature ] )  
                    { special_fn_and_proc_attribute
                      | alterable_fn_and_proc_attribute [ ... ] 
                        [ RESTRICT ] }

subprogram_signature ::= arg_decl [ , ... ]

arg_decl ::= [ arg_name ] [ arg_mode ] arg_type

special_fn_and_proc_attribute ::= RENAME TO subprogram_name
                                  | OWNER TO 
                                    { role_name
                                      | CURRENT_ROLE
                                      | CURRENT_USER
                                      | SESSION_USER }
                                  | SET SCHEMA schema_name
                                  | [ NO ] DEPENDS ON EXTENSION 
                                    extension_name

alterable_fn_and_proc_attribute ::= SET configuration_parameter 
                                    { TO value
                                      | = value
                                      | FROM CURRENT }
                                    | RESET configuration_parameter
                                    | RESET ALL
                                    | [ EXTERNAL ] SECURITY 
                                      { INVOKER | DEFINER }
```
