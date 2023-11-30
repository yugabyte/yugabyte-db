```output.ebnf
update ::= [ with_clause ]  UPDATE table_expr [ [ AS ] alias ]  SET 
           update_item [ , ... ] [ WHERE boolean_expression
                                   | WHERE CURRENT OF cursor_name ]  
           [ returning_clause ]

returning_clause ::= RETURNING { * | { output_expression 
                                     [ [ AS ] output_name ] } 
                                     [ , ... ] }

update_item ::= column_name = column_value
                | ( column_names ) = [ ROW ] ( column_values )
                | ( column_names ) = subquery

column_values ::= { expression | DEFAULT } [ , ... ]

column_names ::= column_name [ , ... ]
```
