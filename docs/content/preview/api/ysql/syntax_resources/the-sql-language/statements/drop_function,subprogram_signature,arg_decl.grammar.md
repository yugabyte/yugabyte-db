```output.ebnf
drop_function ::= DROP FUNCTION [ IF EXISTS ]  
                  { subprogram_name ( [ subprogram_signature ] ) } 
                  [ , ... ] [ CASCADE | RESTRICT ]

subprogram_signature ::= arg_decl [ , ... ]

arg_decl ::= [ formal_arg ] [ arg_mode ] arg_type
```
