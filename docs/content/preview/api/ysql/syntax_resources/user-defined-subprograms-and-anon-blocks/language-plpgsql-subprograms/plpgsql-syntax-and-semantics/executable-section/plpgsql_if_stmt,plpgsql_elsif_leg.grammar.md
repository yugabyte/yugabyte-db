```output.ebnf
plpgsql_if_stmt ::= IF guard_expression THEN 
                    [ plpgsql_executable_stmt [ ... ] ]  
                    [ plpgsql_elsif_leg [ ... ] ]  
                    [ ELSE [ plpgsql_executable_stmt [ ... ] ] ]  END 
                    IF

plpgsql_elsif_leg ::= { ELSIF | ELSEIF } guard_expression THEN 
                      [ plpgsql_executable_stmt [ ... ] ]
```
