# Errors

`pg_hint_plan` stops hint parsing on any error and will uses the hints
already parsed.  Here are some typical errors.

## Syntax errors

Any syntactical errors or wrong hint names are reported as a syntax error.
These errors are reported in the server log with the message level specified
by `pg_hint_plan.message_level` if `pg_hint_plan.debug_print` is on and
above.

## Incorrect Object definitions

Incorrect object definitions result in silently ignoring the hints. This kind
of error is reported as a "Not Used Hint" in the server logs.

## Redundant or conflicting hints

The last hint is considered when redundant hints are defined or hints
conflict with each other.  This kind of error is reported as a duplicated
hints.

## Nested comments

Hint comments cannot be recursive.   If detected, hint parsing is immediately
stopped and all the hints already parsed are ignored.
