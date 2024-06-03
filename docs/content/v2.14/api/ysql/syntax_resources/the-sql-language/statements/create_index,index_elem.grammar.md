```output.ebnf
create_index ::= CREATE [ UNIQUE ] INDEX 
                 [ CONCURRENTLY | NONCONCURRENTLY ]  
                 [ [ IF NOT EXISTS ] name ] ON [ ONLY ] table_name  
                 [ USING access_method_name ] ( index_elem [ , ... ] ) 
                  [ INCLUDE ( column_name [ , ... ] ) ]  
                 [ TABLESPACE tablespace_name ]  
                 [ WHERE boolean_expression ]

index_elem ::= { column_name | ( expression ) } 
               [ operator_class_name ] [ HASH | ASC | DESC ] 
               [ NULLS { FIRST | LAST } ]
```
