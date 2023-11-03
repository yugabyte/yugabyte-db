```output.ebnf
fetch ::= FETCH [ fetch_one_row | fetch_many_rows ] [ FROM | IN ] name

fetch_one_row ::= FIRST
                  | LAST
                  | ABSOLUTE int_literal
                  | NEXT
                  | FORWARD
                  | PRIOR
                  | BACKWARD
                  | RELATIVE int_literal

fetch_many_rows ::= ALL | FORWARD ALL
                    | FORWARD int_literal
                    | int_literal
                    | BACKWARD ALL
                    | BACKWARD int_literal
```
