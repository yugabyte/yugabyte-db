```output.ebnf
create_sequence ::= CREATE [ TEMPORARY | TEMP ] SEQUENCE 
                    [ IF NOT EXISTS ] sequence_name sequence_options

sequence_name ::= qualified_name

sequence_options ::= [ INCREMENT [ BY ] int_literal ]  
                     [ MINVALUE int_literal | NO MINVALUE ] 
                     [ MAXVALUE int_literal | NO MAXVALUE ] 
                     [ START [ WITH ] int_literal ]  
                     [ CACHE positive_int_literal ] [ [ NO ] CYCLE ]
```
