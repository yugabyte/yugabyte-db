# Installation

This section describes the installation steps.

## building binary module

Simply run `make` at the top of the source tree, then `make install` as an
appropriate user. The `PATH` environment variable should be set properly
to point to a PostgreSQL set of binaries:

    $ tar xzvf pg_hint_plan-1.x.x.tar.gz
    $ cd pg_hint_plan-1.x.x
    $ make
    $ su
    $ make install

## Loading `pg_hint_plan`

`pg_hint_plan` does not require `CREATE EXTENSION`.  Loading it with a `LOAD`
command will activate it and of course you can load it globally by setting
`shared_preload_libraries` in `postgresql.conf`.  Or you might be
interested in `ALTER USER SET`/`ALTER DATABASE SET` for automatic loading in
specific sessions.

```sql
postgres=# LOAD 'pg_hint_plan';
LOAD
```

Run `CREATE EXTENSION` and `SET pg_hint_plan.enable_hint_tables TO on` if you
are planning to use the hint table.
