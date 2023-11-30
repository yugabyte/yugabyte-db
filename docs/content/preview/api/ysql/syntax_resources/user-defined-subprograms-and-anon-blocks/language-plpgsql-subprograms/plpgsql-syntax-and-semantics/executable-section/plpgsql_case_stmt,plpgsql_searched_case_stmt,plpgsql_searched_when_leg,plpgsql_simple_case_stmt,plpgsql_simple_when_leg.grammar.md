```output.ebnf
plpgsql_case_stmt ::= plpgsql_searched_case_stmt
                      | plpgsql_simple_case_stmt

plpgsql_searched_case_stmt ::= CASE  plpgsql_searched_when_leg [ ... ] 
                                [ ELSE 
                                  [ plpgsql_executable_stmt [ ... ] ] ] 
                                END CASE

plpgsql_searched_when_leg ::= WHEN guard_expression THEN 
                              [ plpgsql_executable_stmt [ ... ] ]

plpgsql_simple_case_stmt ::= CASE target_expression  
                             plpgsql_simple_when_leg [ ... ]  
                             [ ELSE 
                               [ plpgsql_executable_stmt [ ... ] ] ]  
                             END CASE

plpgsql_simple_when_leg ::= WHEN candidate_expression THEN 
                            [ plpgsql_executable_stmt [ ... ] ]
```
