```
create_index ::= CREATE [ UNIQUE ] INDEX [ [ IF NOT EXISTS ] name ]  
                 ON [ ONLY ] table_name ( index_elem [ , ... ] )  
                 [ INCLUDE ( column_name [ , ... ] ) ]  
                 [ WHERE predicate ]

index_elem ::= { column_name | ( expression ) } [ opclass ] 
               [ ASC | DESC ]
```
