```output.ebnf
plpgsql_executable_stmt ::= plpgsql_basic_stmt | plpgsql_compound_stmt

plpgsql_basic_stmt ::= { NULL | plpgsql_assert_stmt
                         | plpgsql_assignment_stmt
                         | plpgsql_close_cursor_stmt
                         | plpgsql_continue_stmt
                         | plpgsql_dynamic_sql_stmt
                         | plpgsql_exit_stmt
                         | plpgsql_fetch_from_cursor_stmt
                         | plpgsql_get_diagnostics_stmt
                         | plpgsql_get_stacked_diagnostics_stmt
                         | plpgsql_move_in_cursor_stmt
                         | plpgsql_open_cursor_stmt
                         | plpgsql_perform_stmt
                         | plpgsql_raise_stmt
                         | plpgsql_return_stmt
                         | plpgsql_static_bare_sql_stmt
                         | plpgsql_static_dml_returning_stmt
                         | plpgsql_static_select_into_stmt } ;

plpgsql_compound_stmt ::= { plpgsql_block_stmt
                            | plpgsql_loop_stmt
                            | plpgsql_if_stmt
                            | plpgsql_case_stmt } ;
```
