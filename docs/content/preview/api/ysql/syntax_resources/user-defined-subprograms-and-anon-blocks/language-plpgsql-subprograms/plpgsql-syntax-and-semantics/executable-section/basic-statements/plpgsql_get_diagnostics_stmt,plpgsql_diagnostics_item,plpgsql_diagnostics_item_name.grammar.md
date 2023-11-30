```output.ebnf
plpgsql_get_diagnostics_stmt ::= GET [ CURRENT ] DIAGNOSTICS 
                                 plpgsql_diagnostics_item [ , ... ]

plpgsql_diagnostics_item ::= { variable_name | arg_name } { := | = } 
                             plpgsql_diagnostics_item_name

plpgsql_diagnostics_item_name ::= PG_CONTEXT | ROW_COUNT | RESULT_OID
```
