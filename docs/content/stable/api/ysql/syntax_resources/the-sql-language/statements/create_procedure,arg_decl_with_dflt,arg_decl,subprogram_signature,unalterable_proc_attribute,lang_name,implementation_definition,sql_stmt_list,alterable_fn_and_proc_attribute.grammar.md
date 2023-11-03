```output.ebnf
create_procedure ::= CREATE [ OR REPLACE ] PROCEDURE subprogram_name ( 
                     [ arg_decl_with_dflt [ , ... ] ] )  
                     { unalterable_proc_attribute
                       | alterable_fn_and_proc_attribute } [ ... ]

arg_decl_with_dflt ::= arg_decl [ { DEFAULT | = } expression ]

arg_decl ::= [ arg_name ] [ arg_mode ] arg_type

subprogram_signature ::= arg_decl [ , ... ]

unalterable_proc_attribute ::= LANGUAGE lang_name
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
```
