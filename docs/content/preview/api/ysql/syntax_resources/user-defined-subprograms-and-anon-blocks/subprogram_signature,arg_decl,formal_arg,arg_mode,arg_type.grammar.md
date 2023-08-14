```output.ebnf
subprogram_signature ::= arg_decl [ , ... ]

arg_decl ::= [ formal_arg ] [ arg_mode ] arg_type

formal_arg ::= name

arg_mode ::= IN | OUT | INOUT | VARIADIC

arg_type ::= type_name
```
