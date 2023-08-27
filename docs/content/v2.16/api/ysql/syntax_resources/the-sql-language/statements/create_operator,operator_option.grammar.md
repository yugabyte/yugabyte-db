```output.ebnf
create_operator ::= CREATE OPERATOR operator_name ( 
                    { FUNCTION = subprogram_name
                      | PROCEDURE = subprogram_name } 
                    [ , operator_option [ ... ] ] )

operator_option ::= LEFTARG = left_type
                    | RIGHTARG = right_type
                    | COMMUTATOR = com_op
                    | NEGATOR = neg_op
                    | RESTRICT = res_proc
                    | JOIN = join_proc
                    | HASHES
                    | MERGES
```
