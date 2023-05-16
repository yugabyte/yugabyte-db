```output.ebnf
alterable_fn_and_proc_attribute ::= SET run_time_parameter 
                                    { TO value
                                      | = value
                                      | FROM CURRENT }
                                    | RESET run_time_parameter
                                    | RESET ALL
                                    | [ EXTERNAL ] SECURITY 
                                      { INVOKER | DEFINER }
```
