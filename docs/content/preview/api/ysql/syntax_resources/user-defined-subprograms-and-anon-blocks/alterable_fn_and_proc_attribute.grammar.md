```output.ebnf
alterable_fn_and_proc_attribute ::= SET configuration_parameter 
                                    { TO value
                                      | = value
                                      | FROM CURRENT }
                                    | RESET configuration_parameter
                                    | RESET ALL
                                    | [ EXTERNAL ] SECURITY 
                                      { INVOKER | DEFINER }
```
