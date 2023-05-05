```output.ebnf
plpgsql_stmt ::= plpgsql_basic_stmt | plpgsql_compound_stmt

plpgsql_basic_stmt ::= { NULL | plpgsql_assignment_stmt
                         | plpgsql_assert_stmt
                         | static_sql_stmt
                         | dynamic_sql_stmt
                         | perform_stmt
                         | exit_stmt
                         | continue_stmt
                         | open_cursor_stmt
                         | move_in_cursor_stmt
                         | fetch_from_cursor_stmt
                         | close_cursor_stmt
                         | raise_stmt
                         | get_diagnostics_stmt
                         | return_stmt } ;

plpgsql_compound_stmt ::= { plpgsql_block_stmt
                            | plpgsql_loop_stmt
                            | plpgsql_if_stmt
                            | plpgsql_case_stmt } ;
```
