# Instance management commands

These commands manage pgrx-managed Postgres instances. Each Postgres version
gets its own instance with its own data directory and port.

## cargo pgrx start [pgXX]

Starts a pgrx-managed Postgres instance.

```bash
cargo pgrx start pg18        # start Postgres 18
cargo pgrx start             # start default version
cargo pgrx start all         # start all configured versions
```

Flags: `--package`, `--manifest-path`, `--postgresql-conf <K=V>`, `--valgrind`

## cargo pgrx stop [pgXX]

Stops a pgrx-managed Postgres instance.

```bash
cargo pgrx stop pg18         # stop Postgres 18
cargo pgrx stop              # stop default version
cargo pgrx stop all          # stop all running instances
```

Flags: `--package`, `--manifest-path`

## cargo pgrx status [pgXX]

Checks whether a pgrx-managed Postgres instance is running.

```bash
cargo pgrx status pg18       # is Postgres 18 running?
cargo pgrx status            # check default version
```

Flags: `--package`, `--manifest-path`

## cargo pgrx connect [pgXX] [dbname]

Opens a `psql` session to a running pgrx-managed Postgres instance.
Unlike `cargo pgrx run`, this does NOT compile or install the extension.

```bash
cargo pgrx connect pg18          # connect to default database
cargo pgrx connect pg18 mydb     # connect to specific database
cargo pgrx connect --pgcli       # use pgcli instead of psql
```

Flags: `--package`, `--manifest-path`, `--pgcli`, `--valgrind`

## When to use

- `start`/`stop`: managing Postgres instances independently of build/test
- `status`: checking instance state before running commands
- `connect`: connecting to an already-running instance without rebuilding
  the extension

Most developers rarely use these directly -- `cargo pgrx run` and
`cargo pgrx test` handle instance lifecycle automatically.
