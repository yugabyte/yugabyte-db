```
create_table ::= CREATE TABLE [ IF NOT EXISTS ] table_name ( 
                 [ table_elem [ , ... ] ] ) 
                 [ WITH ( storage_param [ , ... ] ) | WITHOUT OIDS ]

table_elem ::= column_name data_type [ column_constraint [ ... ] ]
               | table_constraint

column_constraint ::= [ CONSTRAINT constraint_name ] 
                      [ NOT NULL
                        | NULL
                        | CHECK ( expression )
                        | DEFAULT expression
                        | PRIMARY KEY
                        | references_clause ]

table_constraint ::= [ CONSTRAINT constraint_name ] 
                     { CHECK ( expression )
                       | PRIMARY KEY ( column_names )
                       | FOREIGN KEY ( column_names ) 
                         references_clause }

storage_parameter ::= name [ = value ]
```
