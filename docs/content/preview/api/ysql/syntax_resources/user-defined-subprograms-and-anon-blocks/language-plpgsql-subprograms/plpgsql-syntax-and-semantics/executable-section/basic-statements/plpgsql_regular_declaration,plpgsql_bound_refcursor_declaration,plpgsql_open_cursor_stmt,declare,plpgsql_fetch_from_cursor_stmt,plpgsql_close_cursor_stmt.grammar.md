```output.ebnf
plpgsql_regular_declaration ::= [ variable ] [ CONSTANT ] 
                                [ data_type ] [ NOT NULL ] 
                                [ := expression ] ;

plpgsql_bound_refcursor_declaration ::= plpgsql_bound_refcursor_name 
                                        [ [ NO ] SCROLL ]  CURSOR 
                                        [ ( plpgsql_cursor_arg 
                                          [ , ... ] ) ]  FOR subquery 
                                        ;

plpgsql_open_cursor_stmt ::= OPEN plpgsql_refcursor_name 
                             [ [ NO ] SCROLL ] FOR subquery

declare ::= DECLARE cursor_name [ BINARY ] [ INSENSITIVE ] 
            [ [ NO ] SCROLL ]  CURSOR [ { WITH | WITHOUT } HOLD ] FOR 
            subquery

plpgsql_fetch_from_cursor_stmt ::= FETCH 
                                   { FIRST
                                     | LAST
                                     | ABSOLUTE int_literal
                                     | NEXT
                                     | FORWARD
                                     | PRIOR
                                     | BACKWARD
                                     | RELATIVE int_literal }  
                                   [ FROM | IN ] name INTO 
                                   plpgsql_into_target [ , ... ]

plpgsql_close_cursor_stmt ::= CLOSE plpgsql_refcursor_name
```
