```output.ebnf
create_index ::= CREATE [ UNIQUE ] INDEX 
                 [ CONCURRENTLY | NONCONCURRENTLY ]  
                 [ [ IF NOT EXISTS ] name ] ON [ ONLY ] table_name  
                 [ USING access_method_name ] ( index_elem [ , ... ] ) 
                  [ INCLUDE ( column_name [ , ... ] ) ]  
                 [ TABLESPACE tablespace_name ]  
                 [ SPLIT { INTO int_literal TABLETS
                           | AT VALUES ( split_row [ , ... ] ) } ] 
                 [ WHERE boolean_expression ]

index_elem ::= { column_name | ( expression ) } 
               [ operator_class_name ] [ HASH | ASC | DESC ] 
               [ NULLS { FIRST | LAST } ]
```
