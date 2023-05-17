```output.ebnf
alter_sequence ::= ALTER SEQUENCE [ IF EXISTS ] sequence_name 
                   alter_sequence_options

name ::= '<Text Literal>'

alter_sequence_options ::= [ AS seq_data_type ] 
                           [ INCREMENT [ BY ] int_literal ] 
                           [ MINVALUE int_literal | NO MINVALUE ] 
                           [ MAXVALUE maxvalue | NO MAXVALUE ] 
                           [ START [ WITH ] int_literal ] 
                           [ RESTART [ [ WITH ] int_literal ] ] 
                           [ CACHE cache ] 
                           [ OWNED BY table_name.table_column | NONE ]
```
