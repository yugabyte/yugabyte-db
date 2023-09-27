```output.ebnf
plpgsql_loop_stmt ::= [ << label >> ] { plpgsql_unbounded_loop_defn
                                        | plpgsql_bounded_loop_defn } 
                      LOOP  [ plpgsql_executable_stmt [ , ... ] ]  
                      END LOOP [ label ]

plpgsql_unbounded_loop_defn ::= [ WHILE boolean_expression ]

plpgsql_bounded_loop_defn ::= plpgsql_integer_for_loop_defn
                              | plpgsql_array_foreach_loop_defn
                              | plpgsql_query_for_loop_defn

plpgsql_integer_for_loop_defn ::= FOR variable IN  [ REVERSE ] 
                                  int_expression .. int_expression  
                                  [ BY int_expression ]

plpgsql_array_foreach_loop_defn ::= FOREACH variable  
                                    [ SLICE int_literal ] IN ARRAY 
                                    array_expression

plpgsql_query_for_loop_defn ::= FOR variable [ variable [ , ... ] ] IN 
                                 { subquery
                                   | plpgsql_bound_refcursor_name
                                   | plpgsql_dynamic_subquery }

plpgsql_dynamic_subquery ::= EXECUTE text_expression 
                             [ USING expression [ , ... ] ]
```
