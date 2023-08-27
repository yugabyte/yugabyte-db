```output.ebnf
move ::= MOVE [ move_to_one_row | move_over_many_rows ] [ FROM | IN ] 
         name

move_to_one_row ::= FIRST
                    | LAST
                    | ABSOLUTE int_literal
                    | NEXT
                    | FORWARD
                    | PRIOR
                    | BACKWARD
                    | RELATIVE int_literal

move_over_many_rows ::= ALL | FORWARD ALL
                        | FORWARD int_literal
                        | int_literal
                        | BACKWARD ALL
                        | BACKWARD int_literal
```
