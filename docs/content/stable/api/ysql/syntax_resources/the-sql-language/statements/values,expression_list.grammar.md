```output.ebnf
values ::= VALUES ( expression_list ) [ ,(expression_list ... ]  
           [ ORDER BY { order_expr [ , ... ] } ]  
           [ LIMIT { int_expression | ALL } ]  
           [ OFFSET int_expression [ ROW | ROWS ] ]  
           [ FETCH { FIRST | NEXT } int_expression { ROW | ROWS } ONLY ]

expression_list ::= expression [ , ... ]
```
