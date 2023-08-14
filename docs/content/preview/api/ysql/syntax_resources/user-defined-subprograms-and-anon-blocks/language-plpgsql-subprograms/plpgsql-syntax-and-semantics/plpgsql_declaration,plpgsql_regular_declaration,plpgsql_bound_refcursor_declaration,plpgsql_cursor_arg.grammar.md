```output.ebnf
plpgsql_declaration ::= plpgsql_regular_declaration
                        | plpgsql_bound_refcursor_declaration

plpgsql_regular_declaration ::= [ variable ] [ CONSTANT ] 
                                [ data_type ] [ NOT NULL ] 
                                [ := expression ] ;

plpgsql_bound_refcursor_declaration ::= plpgsql_bound_refcursor_name 
                                        [ [ NO ] SCROLL ]  CURSOR 
                                        [ ( plpgsql_cursor_arg 
                                          [ , ... ] ) ]  FOR subquery 
                                        ;

plpgsql_cursor_arg ::= formal_arg arg_type
```
