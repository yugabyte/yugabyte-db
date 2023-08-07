```output.ebnf
plpgsql_block_stmt ::= [ << label >> ]  
                       [ plpgsql_declaration_section ]  
                       plpgsql_executable_section  
                       [ plpgsql_exception_section ] END [ label ] ;

plpgsql_declaration_section ::= DECLARE 
                                [ plpgsql_declaration [ ... ] ]

plpgsql_executable_section ::= BEGIN 
                               [ plpgsql_executable_stmt [ ... ] ]

plpgsql_exception_section ::= EXCEPTION { plpgsql_handler [ ... ] }
```
