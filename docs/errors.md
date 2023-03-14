# Errors

`pg_hint_plan` stops parsing on any error and uses hints already parsed on the
most cases. Following are the typical errors.

## Syntax errors

Any syntactical errors or wrong hint names are reported as a syntax error.
These errors are reported in the server log with the message level specified by
`pg_hint_plan.message_level` if `pg_hint_plan.debug_print` is on and above.

## Object misspecifications

Object misspecifications result in silent ignorance of the hints. This kind of
error is reported as "not used hints" in the server log by the same condition
as syntax errors.

## Redundant or conflicting hints

The last hint will be active when redundant hints or hints conflicting with
each other. This kind of error is reported as "duplication hints" in the server
log by the same condition to syntax errors.

## Nested comments

Hint comment cannot include another block comment within. If `pg_hint_plan`
finds it, differently from other erros, it stops parsing and abandans all hints
already parsed. This kind of error is reported in the same manner as other
errors.
