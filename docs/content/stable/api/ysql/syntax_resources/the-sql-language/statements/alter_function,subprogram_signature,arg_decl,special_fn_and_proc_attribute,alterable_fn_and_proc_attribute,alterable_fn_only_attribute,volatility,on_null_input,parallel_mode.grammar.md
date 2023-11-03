```output.ebnf
alter_function ::= ALTER FUNCTION subprogram_name ( 
                   [ subprogram_signature ] )  
                   { special_fn_and_proc_attribute
                     | { alterable_fn_and_proc_attribute
                         | alterable_fn_only_attribute } [ ... ] 
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

alterable_fn_and_proc_attribute ::= SET run_time_parameter 
                                    { TO value
                                      | = value
                                      | FROM CURRENT }
                                    | RESET run_time_parameter
                                    | RESET ALL
                                    | [ EXTERNAL ] SECURITY 
                                      { INVOKER | DEFINER }

alterable_fn_only_attribute ::= volatility
                                | on_null_input
                                | PARALLEL parallel_mode
                                | [ NOT ] LEAKPROOF
                                | COST int_literal
                                | ROWS int_literal

volatility ::= IMMUTABLE | STABLE | VOLATILE

on_null_input ::= CALLED ON NULL INPUT
                  | RETURNS NULL ON NULL INPUT
                  | STRICT

parallel_mode ::= UNSAFE | RESTRICTED | SAFE
```
