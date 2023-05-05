```output.ebnf
create_function ::= CREATE [ OR REPLACE ] FUNCTION subprogram_name ( 
                    [ arg_decl_with_dflt [ , ... ] ] )  
                    { RETURNS data_type
                      | RETURNS TABLE ( { column_name data_type } 
                        [ , ... ] ) }  
                    { unalterable_fn_attribute
                      | alterable_fn_only_attribute
                      | alterable_fn_and_proc_attribute } [ ... ]

arg_decl_with_dflt ::= arg_decl [ { DEFAULT | = } expression ]

arg_decl ::= [ arg_name ] [ arg_mode ] arg_type

subprogram_signature ::= arg_decl [ , ... ]

unalterable_fn_attribute ::= WINDOW
                             | LANGUAGE lang_name
                             | AS implementation_definition

lang_name ::= SQL | PLPGSQL | C

implementation_definition ::= ' sql_stmt_list '
                              | ' plpgsql_block_stmt '
                              | ' obj_file ' [ , ' link_symbol ' ]

sql_stmt_list ::= sql_stmt ; [ sql_stmt ... ]

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
