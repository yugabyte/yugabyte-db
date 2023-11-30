```output.ebnf
plpgsql_get_stacked_diagnostics_stmt ::= GET STACKED DIAGNOSTICS 
                                         plpgsql_stacked_diagnostics_item 
                                         [ , ... ]

plpgsql_stacked_diagnostics_item ::= { variable_name | arg_name } 
                                     { := | = } 
                                     plpgsql_stacked_diagnostics_item_name

plpgsql_stacked_diagnostics_item_name ::= RETURNED_SQLSTATE
                                          | MESSAGE_TEXT
                                          | PG_EXCEPTION_DETAIL
                                          | PG_EXCEPTION_HINT
                                          | SCHEMA_NAME
                                          | TABLE_NAME
                                          | COLUMN_NAME
                                          | PG_DATATYPE_NAME
                                          | CONSTRAINT_NAME
                                          | PG_EXCEPTION_CONTEXT
```
