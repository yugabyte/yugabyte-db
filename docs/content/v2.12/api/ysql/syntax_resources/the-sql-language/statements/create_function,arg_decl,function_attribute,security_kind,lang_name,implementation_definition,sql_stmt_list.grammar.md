```output.ebnf
create_function ::= CREATE [ OR REPLACE ] FUNCTION function_name ( 
                    [ arg_decl [ , ... ] ] )  
                    [ RETURNS data_type
                      | RETURNS TABLE ( { column_name data_type } 
                        [ , ... ] ) ]  function_attribute [ ... ]

arg_decl ::= [ argmode ] [ argname ] argtype 
             [ { DEFAULT | = } expression ]

function_attribute ::= WINDOW
                       | IMMUTABLE
                       | STABLE
                       | VOLATILE
                       | [ NOT ] LEAKPROOF
                       | CALLED ON NULL INPUT
                       | RETURNS NULL ON NULL INPUT
                       | STRICT
                       | PARALLEL { UNSAFE | RESTRICTED | SAFE }
                       | COST int_literal
                       | ROWS int_literal
                       | TRANSFORM { FOR TYPE type_name } [ , ... ]
                       | SET configuration_parameter 
                         { TO value | = value | FROM CURRENT }
                       | [ EXTERNAL ] SECURITY security_kind
                       | LANGUAGE lang_name
                       | AS implementation_definition

security_kind ::= INVOKER | DEFINER

lang_name ::= SQL | PLPGSQL | C

implementation_definition ::= ' sql_stmt_list '
                              | ' plpgsql_block_stmt '
                              | ' obj_file ' [ , ' link_symbol ' ]

sql_stmt_list ::= sql_stmt ; [ sql_stmt ... ]
```
