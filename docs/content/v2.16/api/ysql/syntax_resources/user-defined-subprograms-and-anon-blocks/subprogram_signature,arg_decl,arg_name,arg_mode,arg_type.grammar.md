```output.ebnf
subprogram_signature ::= arg_decl [ , ... ]

arg_decl ::= [ arg_name ] [ arg_mode ] arg_type

arg_name ::= name

arg_mode ::= IN | OUT | INOUT | VARIADIC

arg_type ::= type_name
```
