An example of how to create an Aggregate with `pgrx`.

Demonstrates how to create a `IntegerAvgState` aggregate.

This example also demonstrates the use of `PgVarlena<T>` and how to use `#[pgvarlena_inoutfuncs]` with `#[derive(PostgresType)]`.