```output.ebnf
create_function ::= CREATE [ OR REPLACE ] FUNCTION subprogram_name ( 
                    [ arg_decl_with_dflt [ , ... ] ] )  
                    { RETURNS data_type
                      | RETURNS TABLE ( { column_name data_type } 
                        [ , ... ] ) }  
                    { unalterable_fn_attribute
                      | alterable_fn_only_attribute
                      | alterable_fn_and_proc_attribute } [ ... ]

create_procedure ::= CREATE [ OR REPLACE ] PROCEDURE subprogram_name ( 
                     [ arg_decl_with_dflt [ , ... ] ] )  
                     { unalterable_proc_attribute
                       | alterable_fn_and_proc_attribute } [ ... ]

alter_function ::= ALTER FUNCTION subprogram_name ( 
                   [ subprogram_signature ] )  
                   { special_fn_and_proc_attribute
                     | { alterable_fn_and_proc_attribute
                         | alterable_fn_only_attribute } [ ... ] 
                       [ RESTRICT ] }

alter_procedure ::= ALTER PROCEDURE subprogram_name ( 
                    [ subprogram_signature ] )  
                    { special_fn_and_proc_attribute
                      | alterable_fn_and_proc_attribute [ ... ] 
                        [ RESTRICT ] }
```
