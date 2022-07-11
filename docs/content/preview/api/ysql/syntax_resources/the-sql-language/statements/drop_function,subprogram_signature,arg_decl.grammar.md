```output.ebnf
drop_function ::= DROP FUNCTION [ IF EXISTS ]  
                  { subprogram_name ( [ subprogram_signature ] ) } 
                  [ , ... ] [ CASCADE | RESTRICT ]

subprogram_signature ::= arg_decl [ , ... ]

arg_decl ::= [ arg_name ] [ arg_mode ] arg_type
```
