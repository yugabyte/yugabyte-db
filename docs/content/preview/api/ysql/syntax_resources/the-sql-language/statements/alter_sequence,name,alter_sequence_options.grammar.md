```output.ebnf
alter_sequence ::= ALTER SEQUENCE [ IF EXISTS ] sequence_name 
                   alter_sequence_options

name ::= text_literal

alter_sequence_options ::= [ AS seq_data_type ]  
                           [ INCREMENT [ BY ] int_literal ]  
                           [ MINVALUE int_literal | NO MINVALUE ]  
                           [ MAXVALUE int_literal | NO MAXVALUE ]  
                           [ START [ WITH ] int_literal ]  
                           [ RESTART [ [ WITH ] int_literal ] ]  
                           [ CACHE int_literal ]  [ [ NO ] CYCLE ]  
                           [ OWNED BY table_name . column_name
                             | NONE ]
```
