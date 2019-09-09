```
create_table ::= CREATE TABLE [ IF NOT EXISTS ] table_name ( 
                 [ table_elem [ , ... ] ] ) 
                 [ WITH ( storage_parameters ) | WITHOUT OIDS ]

table_elem ::= column_name data_type [ column_constraint [ ... ] ]
               | table_constraint

column_constraint ::= [ CONSTRAINT constraint_name ] 
                      { NOT NULL
                        | NULL
                        | CHECK ( expression )
                        | DEFAULT expression
                        | UNIQUE index_parameters
                        | PRIMARY KEY
                        | references_clause }

table_constraint ::= [ CONSTRAINT constraint_name ] 
                     { CHECK ( expression )
                       | UNIQUE ( column_names ) index_parameters
                       | PRIMARY KEY ( column_names )
                       | FOREIGN KEY ( column_names ) 
                         references_clause }

storage_parameters ::= storage_parameter [ , ... ]

storage_parameter ::= param_name [ = param_value ]

index_parameters ::= [ INCLUDE ( column_names ) ] 
                     [ WITH ( storage_parameters ) ]
```
