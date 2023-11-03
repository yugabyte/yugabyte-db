```output.ebnf
create_procedure ::= CREATE [ OR REPLACE ] PROCEDURE name ( 
                     [ arg_decl [ , ... ] ] ) procedure_attribute 
                     [ ... ]

arg_decl ::= [ argmode ] [ argname ] argtype 
             [ { DEFAULT | = } expression ]

procedure_attribute ::= TRANSFORM { FOR TYPE type_name } [ , ... ]
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
