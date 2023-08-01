```output.ebnf
plpgsql_raise_stmt ::= RAISE [ plpgsql_raise_level ] 
                       [ plpgsql_raise_shortcut_for_exception_or_message ] 
                        [ USING plpgsql_raise_using_item [ , ... ] ]

plpgsql_raise_level ::= DEBUG
                        | LOG
                        | NOTICE
                        | WARNING
                        | EXCEPTION
                        | INFO

plpgsql_raise_shortcut_for_exception_or_message ::= SQLSTATE 
                                                    errcode_literal
                                                    | exception_name
                                                    | message_literal 
                                                      [ text_expression 
                                                        [ , ... ] ]

plpgsql_raise_using_item ::= { ERRCODE
                               | MESSAGE
                               | DETAIL
                               | HINT
                               | SCHEMA
                               | TABLE
                               | COLUMN
                               | DATATYPE
                               | CONSTRAINT } { := | = } 
                             text_expression
```
