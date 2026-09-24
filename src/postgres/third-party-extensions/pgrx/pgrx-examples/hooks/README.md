# A basic safety catch extension

This example demonstrates how to declare PostgreSQL hooks
and trigger Rust function to interfere with various database
operations.

## Context

Up until version 0.15, PGRX had a trait named PgHooks that
was useful to register PostgreSQL hooks. In version 0.16,
this trait was removed.

Registering a hooks is still possible but requires some extra
work.

## Principles

This extension will enforce security mesures to avoid human
errors during data suppression commands :

* DELETE commands must have a WHERE clause
* Only superusers are allowed to use the TRUNCATE command

## Similar extensions 

* https://github.com/eradman/pg-safeupdate 
