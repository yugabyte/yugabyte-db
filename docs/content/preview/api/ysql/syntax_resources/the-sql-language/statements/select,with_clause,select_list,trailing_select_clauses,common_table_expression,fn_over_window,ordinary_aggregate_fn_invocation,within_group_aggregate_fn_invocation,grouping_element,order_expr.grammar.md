```output.ebnf
select ::= [ with_clause ] SELECT select_list 
           [ trailing_select_clauses ]

with_clause ::= WITH [ RECURSIVE ] 
                { common_table_expression [ , ... ] }

select_list ::= [ ALL | DISTINCT [ ON { ( expression [ , ... ] ) } ] ] 
                 [ * | { { expression
                           | fn_over_window
                           | ordinary_aggregate_fn_invocation
                           | within_group_aggregate_fn_invocation } 
                       [ [ AS ] name ] } [ , ... ] ]

trailing_select_clauses ::= [ FROM { from_item [ , ... ] } ]  
                            [ WHERE boolean_expression ]  
                            [ GROUP BY { grouping_element [ , ... ] } ] 
                             [ HAVING boolean_expression ]  
                            [ WINDOW 
                              { { name AS window_definition } 
                              [ , ... ] } ]  
                            [ { UNION | INTERSECT | EXCEPT } 
                              [ ALL | DISTINCT ] select ]  
                            [ ORDER BY { order_expr [ , ... ] } ]  
                            [ LIMIT { int_expression | ALL } ]  
                            [ OFFSET int_expression [ ROW | ROWS ] ]  
                            [ FETCH { FIRST | NEXT } int_expression 
                              { ROW | ROWS } ONLY ]  
                            [ FOR { UPDATE
                                    | NO KEY UPDATE
                                    | SHARE
                                    | KEY SHARE } 
                              [ OF table_name [ , ... ] ] 
                              [ NOWAIT | SKIP LOCKED ] [ ... ] ]

common_table_expression ::= name [ ( name [ , ... ] ) ] AS ( 
                            { select
                              | values
                              | insert
                              | update
                              | delete } )

fn_over_window ::= name  ( [ expression [ , ... ] | * ]  
                   [ FILTER ( WHERE boolean_expression ) ] OVER 
                   { window_definition | name }

ordinary_aggregate_fn_invocation ::= name  ( 
                                     { [ ALL | DISTINCT ] expression 
                                       [ , ... ]
                                       | * } 
                                     [ ORDER BY order_expr [ , ... ] ] 
                                     )  [ FILTER ( WHERE 
                                          boolean_expression ) ]

within_group_aggregate_fn_invocation ::= name  ( 
                                         { expression [ , ... ] } )  
                                         WITHIN GROUP ( ORDER BY 
                                         order_expr [ , ... ] )  
                                         [ FILTER ( WHERE 
                                           boolean_expression ) ]

grouping_element ::= ( ) | ( expression [ , ... ] )
                     | ROLLUP ( expression [ , ... ] )
                     | CUBE ( expression [ , ... ] )
                     | GROUPING SETS ( grouping_element [ , ... ] )

order_expr ::= expression [ ASC | DESC | USING operator_name ] 
               [ NULLS { FIRST | LAST } ]
```
