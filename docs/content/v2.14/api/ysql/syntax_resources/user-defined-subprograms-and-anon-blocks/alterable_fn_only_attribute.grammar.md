```output.ebnf
alterable_fn_only_attribute ::= volatility
                                | on_null_input
                                | PARALLEL parallel_mode
                                | [ NOT ] LEAKPROOF
                                | COST int_literal
                                | ROWS int_literal
```
