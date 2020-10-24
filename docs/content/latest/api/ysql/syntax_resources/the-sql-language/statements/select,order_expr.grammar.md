```
select ::= [ WITH [ RECURSIVE ] { with_query [ , ... ] } ]  SELECT 
           [ ALL | DISTINCT [ ON { ( expression [ , ... ] ) } ] ] 
           [ * | { { expression
                     | fn_over_window
                     | ordinary_aggregate_fn_invocation
                     | within_group_aggregate_fn_invocation } 
                 [ [ AS ] name ] } [ , ... ] ]  
           [ FROM { from_item [ , ... ] } ]  
           [ WHERE boolean_expression ]  
           [ GROUP BY { grouping_element [ , ... ] } ]  
           [ HAVING boolean_expression ]  
           [ WINDOW { { name AS window_definition } [ , ... ] } ]  
           [ { UNION | INTERSECT | EXCEPT } [ ALL | DISTINCT ] select ] 
            [ ORDER BY { order_expr [ , ... ] } ]  
           [ LIMIT { integer | ALL } ]  
           [ OFFSET integer [ ROW | ROWS ] ]  
           [ FETCH { FIRST | NEXT } integer { ROW | ROWS } ONLY ]

order_expr ::= expression [ ASC | DESC | USING operator_name ] 
               [ NULLS { FIRST | LAST } ]
```
