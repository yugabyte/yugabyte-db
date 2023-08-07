```output.ebnf
plpgsql_exception_section ::= EXCEPTION { plpgsql_handler [ ... ] }

plpgsql_handler ::= WHEN { plpgsql_handler_condition [ OR ... ] } THEN 
                    { plpgsql_executable_stmt [ ... ] }

plpgsql_handler_condition ::= SQLSTATE errcode_literal
                              | exception_name
                              | OTHERS
```
