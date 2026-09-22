# cargo-pgrx

`cargo-pgrx` is a Cargo subcommand for managing `pgrx`-based Postgres extensions.

You'll want to use `cargo pgrx` during your extension development process. It automates the process of creating new Rust crate projects, auto-generating the SQL schema for your extension, installing your extension locally for testing with Postgres, and running your test suite against one or more versions of Postgres.

A video walkthrough of its abilities can be found here: https://www.twitch.tv/videos/684087991

## Installing

Install via crates.io:

```console
$ cargo install --locked cargo-pgrx
```

As new versions of `pgrx` are released, you'll want to make sure you run this command again to update it.

Note that some of the features of PGRX involve compiling C code, including `cargo pgrx init`, and
as such you will also need a toolchain for doing so and potentially must provide various libraries.
Normally, Rust requires a C toolchain anyways, but it does not require e.g. pkg-config.

## Usage

```console
$ cargo pgrx --help
Cargo subcommand for 'pgrx' to make Postgres extension development easy

Usage: cargo pgrx [OPTIONS] <COMMAND>

Commands:
  bench         Run in-process benchmarks for this crate
  init          Initialize pgrx development environment for the first time
  info          Provides information about pgrx-managed development environment
  start         Start a pgrx-managed Postgres instance
  stop          Stop a pgrx-managed Postgres instance
  status        Is a pgrx-managed Postgres instance running?
  new           Create a new extension crate
  install       Install the crate as an extension into the Postgres specified by `pg_config`
  package       Create an installation package directory
  schema        Generate extension schema files
  run           Compile/install extension to a pgrx-managed Postgres instance and start psql
  connect       Connect, via psql, to a Postgres instance
  test          Run the test suite for this crate
  get           Get a property from the extension control file
  cross         Commands having to do with cross-compilation. (Experimental)
  upgrade       Upgrade pgrx crate versions in `Cargo.toml`. Defaults to latest
  regress       Run the regression test suite for this crate
  help          Print this message or the help of the given subcommand(s)

Options:
  -v, --verbose...  Enable info logs, -vv for debug, -vvv for trace
  -h, --help        Print help
  -V, --version     Print version
```

## Environment Variables

- `PGRX_HOME` - Defaults to "${HOME}/.pgrx/" if not set.
- `PGRX_BUILD_FLAGS` - If set during `cargo pgrx run/test/install/package/bench`, these additional flags are passed to `cargo build` while building the extension
- `PGRX_BUILD_VERBOSE` - Set to true to enable verbose "build.rs" output -- useful for debugging build issues
- `HTTPS_PROXY` - If set during `cargo pgrx init`, it will download the Postgres sources using these proxy settings. For more details refer to the [env_proxy crate documentation](https://docs.rs/env_proxy/*/env_proxy/fn.for_url.html).
- `PGRX_IGNORE_RUST_VERSIONS` - Set to true to disable the `rustc` version check we have when performing schema generation (schema generation requires the same version of `rustc` be used to build `cargo-pgrx` as the crate in question).
- `CARGO_PGRX_DISABLE_GC_SECTIONS_WORKAROUND` - Set to any value to stop `cargo-pgrx` from injecting `-C link-arg=-Wl,--no-gc-sections` on non-macOS Unix cdylib builds. By default, `cargo-pgrx` passes this flag (scoped to `cfg(all(target_family = "unix", not(target_os = "macos")))`) to cargo as `--config target.<cfg>.rustflags=[...]` only for cdylib-producing invocations — `cargo pgrx install`, `cargo pgrx package`, and the non-`--test` path of `cargo pgrx schema`. This prevents GNU `ld --gc-sections` from dropping the `.pgrxsc` section where the embedded SQL schema metadata lives — a problem that surfaces on non-LTO builds (notably aarch64 Linux) as `no embedded pgrx schema section found`. The workaround is intentionally not applied to `cargo pgrx test` / `cargo pgrx schema --test` because those also link a test binary whose undefined Postgres `extern "C"` references are expected to be GC'd away. Disable this only if you are explicitly managing `rustflags` yourself and understand the retention contract.

## First Time Initialization

```console
$ cargo pgrx init
   Discovered Postgres v12.16, v13.12, v14.9, v15.4, v16.0
  Downloading Postgres v14.9 from https://ftp.postgresql.org/pub/source/v14.9/postgresql-14.9.tar.bz2
  Downloading Postgres v15.4 from https://ftp.postgresql.org/pub/source/v15.4/postgresql-15.4.tar.bz2
  Downloading Postgres v12.16 from https://ftp.postgresql.org/pub/source/v12.16/postgresql-12.16.tar.bz2
  Downloading Postgres v13.12 from https://ftp.postgresql.org/pub/source/v13.12/postgresql-13.12.tar.bz2
  Downloading Postgres v16.0 from https://ftp.postgresql.org/pub/source/v16.0/postgresql-16.0.tar.bz2
     Removing /home/you/.pgrx/12.16_unpack
     Removing /home/you/.pgrx/14.9_unpack
    Untarring Postgres v12.16 to /home/you/.pgrx/12.16_unpack
    Untarring Postgres v14.9 to /home/you/.pgrx/14.9_unpack
     Removing /home/you/.pgrx/15.4_unpack
    Untarring Postgres v15.4 to /home/you/.pgrx/15.4_unpack
     Removing /home/you/.pgrx/16.0_unpack
    Untarring Postgres v16.0 to /home/you/.pgrx/16.0_unpack
     Removing /home/you/.pgrx/13.12_unpack
    Untarring Postgres v13.12 to /home/you/.pgrx/13.12_unpack
     Removing /home/you/.pgrx/12.16
     Removing /home/you/.pgrx/14.9
     Renaming /home/you/.pgrx/12.16_unpack/postgresql-12.16 -> /home/you/.pgrx/12.16
  Configuring Postgres v12.16
     Renaming /home/you/.pgrx/14.9_unpack/postgresql-14.9 -> /home/you/.pgrx/14.9
  Configuring Postgres v14.9
     Removing /home/you/.pgrx/15.4
     Renaming /home/you/.pgrx/15.4_unpack/postgresql-15.4 -> /home/you/.pgrx/15.4
  Configuring Postgres v15.4
     Removing /home/you/.pgrx/16.0
     Renaming /home/you/.pgrx/16.0_unpack/postgresql-16.0 -> /home/you/.pgrx/16.0
  Configuring Postgres v16.0
     Removing /home/you/.pgrx/13.12
     Renaming /home/you/.pgrx/13.12_unpack/postgresql-13.12 -> /home/you/.pgrx/13.12
  Configuring Postgres v13.12
    Compiling Postgres v16.0
    Compiling Postgres v12.16
    Compiling Postgres v14.9
    Compiling Postgres v15.4
    Compiling Postgres v13.12
   Installing Postgres v12.16 to /home/you/.pgrx/12.16/pgrx-install
   Installing Postgres v13.12 to /home/you/.pgrx/13.12/pgrx-install
   Installing Postgres v14.9 to /home/you/.pgrx/14.9/pgrx-install
   Installing Postgres v15.4 to /home/you/.pgrx/15.4/pgrx-install
   Installing Postgres v16.0 to /home/you/.pgrx/16.0/pgrx-install
   Validating /home/you/.pgrx/12.16/pgrx-install/bin/pg_config
   Validating /home/you/.pgrx/13.12/pgrx-install/bin/pg_config
   Validating /home/you/.pgrx/14.9/pgrx-install/bin/pg_config
   Validating /home/you/.pgrx/15.4/pgrx-install/bin/pg_config
   Validating /home/you/.pgrx/16.0/pgrx-install/bin/pg_config
```

`cargo pgrx init` is required to be run once to properly configure the `pgrx` development environment.

As shown by the screenshot above, it downloads the latest releases of supported Postgres versions,
configures them for debugging, compiles them with assertions, and installs them to "${PGRX_HOME}".
These include all [`contrib`] extensions and tools included with Postgres.
Other `cargo pgrx` commands such as `run` and `test` will manage and use these installations on
your behalf.

[`contrib`]: https://www.postgresql.org/docs/current/contrib.html

`pgrx` is designed to support multiple Postgres versions in such a way that during development,
you'll know if you're trying to use a Postgres API that isn't common across all versions.
It is also designed to make testing your extension against these versions easy.
This is why it enables debug symbols and the database assertions, and why it expects all versions
of Postgres that your extension supports to be installed during development.

In cases when default ports pgrx uses to run PostgreSQL within are not available, one can specify
custom values for these during initialization using `--base-port` and `--base-testing-port`
options. One of the use cases for this is using multiple installations of pgrx (using different
"$PGRX_HOME"s) when developing multiple extensions at the same time.
These values can be later changed in "$PGRX_HOME/config.toml".

If you want to use your operating system's package manager to install Postgres, `cargo pgrx init`
has optional arguments that allow you to specify where they're installed (see below).
Be aware, this may result in different behavior than a database compiled by cargo-pgrx, where
tests that would have been failed by an internal Postgres assertion instead successfully pass.
This can be problematic if you are using `pgrx-pg-sys` directly, as those assertions are often the
only thing that will catch directly misusing the Postgres extension API!

Each version is specified as `"--pg${VER}` and the full path to `pg_config` for that version.
For any version you specify, `cargo pgrx init` will forego downloading/compiling/installing it.
cargo-pgrx will then use that locally-installed version the same way it uses any version it
compiles and installs itself.

However, if the "path to pg_config" is the literal string `download`, then cargo-pgrx will download
and compile that version of Postgres for you. When any `"--pg${VER}"` option is specified,
these are the **only** versions of Postgres that cargo-pgrx will manage for you.

You'll also want to make sure you have the "postgresql-server-dev" package installed for each version you want to manage yourself. If you need to customize the configuration of the Postgres build, you can use `--configure-flag` to pass optins to the `configure` script. For example, you could use `--configure-flag=--with-ssl=openssl` to enable SSL support or `--configure-flag=--with-libraries=/path/to/libs` to use a non-standard location for dependency libraries. This flag can be used multiple times to pass multiple configuration options.

Once complete, `cargo pgrx init` also creates "${PGRX_HOME}/config.toml" which describes where to
find each version's `pg_config` tool.

If a new minor Postgres version is released in the future you can simply run `cargo pgrx init [args]` again, and your local version will be updated, preserving all existing databases and configuration.

```console
$ cargo pgrx init -h
Initialize pgrx development environment for the first time

Usage: cargo pgrx init [OPTIONS]

Options:
      --pg13 <PG13>                            If installed locally, the path to PG13's `pgconfig` tool, or `download`
                                               to have pgrx download/compile/install it [env: PG13_PG_CONFIG=]
  -v, --verbose...                             Enable info logs, -vv for debug, -vvv for trace
      --pg14 <PG14>                            If installed locally, the path to PG14's `pgconfig` tool, or `download`
                                               to have pgrx download/compile/install it [env: PG14_PG_CONFIG=]
      --pg15 <PG15>                            If installed locally, the path to PG15's `pgconfig` tool, or `download`
                                               to have pgrx download/compile/install it [env: PG15_PG_CONFIG=]
      --pg16 <PG16>                            If installed locally, the path to PG16's `pgconfig` tool, or `download`
                                               to have pgrx download/compile/install it [env: PG16_PG_CONFIG=]
      --pg17 <PG17>                            If installed locally, the path to PG17's `pgconfig` tool, or `download`
                                               to have pgrx download/compile/install it [env: PG17_PG_CONFIG=]
      --base-port <BASE_PORT>                  Base port number
      --base-testing-port <BASE_TESTING_PORT>  Base testing port number
      --configure-flag <CONFIGURE_FLAG>        Additional flags to pass to the configure script
      --valgrind                               Compile PostgreSQL with the necessary flags to detect a good amount of memory errors when run under Valgrind
  -j, --jobs <JOBS>                            Allow N make jobs at once
  -h, --help                                   Print help (see more with '--help')
  -V, --version                                Print version```

## Creating a new Extension

```rust
$ cargo pgrx new example
$ ls example/
Cargo.toml  example.control  sql  src
```

`cargo pgrx new <extname>` is an easy way to get started creating a new extension. It's similar to `cargo new <name>`, but does the additional things necessary to support building a Rust Postgres extension.

If you'd like to create a "background worker" instead, specify the `--bgworker` argument.

`cargo pgrx new` does not initialize the directory as a git repo, but it does create a `.gitignore` file in case you decide to do so.

> **Workspace users:** `cargo pgrx new $NAME` will create a `$NAME/.cargo/config.toml`, you should move this into your workspace root as `.cargo/config.toml`.
>
> If you don't, you may experience unnecessary rebuilds using tools like Rust-Analyzer, as it will use the wrong `rustflags` option.

```console
$ cargo pgrx new --help
cargo-pgrx-new 0.5.0
PgCentral Foundation, Inc. <contact@pgcentral.org>
Create a new extension crate

USAGE:
    cargo pgrx new [OPTIONS] <NAME>

ARGS:
    <NAME>    The name of the extension

OPTIONS:
    -b, --bgworker    Create a background worker template
    -h, --help        Print help information
    -v, --verbose     Enable info logs, -vv for debug, -vvv for trace
    -V, --version     Print version information
```

## Managing Your Postgres Installations

```console
$ cargo pgrx status all
Postgres v12 is stopped
Postgres v13 is stopped
Postgres v14 is stopped
Postgres v15 is stopped
Postgres v16 is stopped

$ cargo pgrx start all
    Starting Postgres v12 on port 28812
    Starting Postgres v13 on port 28813
    Starting Postgres v14 on port 28814
    Starting Postgres v15 on port 28815
    Starting Postgres v16 on port 28816

$ cargo pgrx status all
Postgres v12 is running
Postgres v13 is running
Postgres v14 is running
Postgres v15 is running
Postgres v16 is running

$ cargo pgrx stop all
    Stopping Postgres v12
    Stopping Postgres v13
    Stopping Postgres v14
    Stopping Postgres v15
    Stopping Postgres v16
```

`cargo pgrx` has three commands for managing Postgres installations: `start`, `stop`, and `status`.
Additionally, `cargo pgrx run` (see below) will automatically start its target Postgres instance if
not already running. Note that when directed to use `"all"` Postgres instances, this is interpreted
in terms of an extension's `pg{MAJOR}` features in its Cargo.toml, except for `cargo pgrx status`.

When starting a Postgres instance, `pgrx` starts it on port `28800 + PG_MAJOR_VERSION`, so
Postgres 15 runs on `28815`, 16 on `28816`, etc. Additionally, the first time any of these are
started, it will initialize `PGDATA` directories in `"${PGRX_HOME}"/data-{12,13,14,15,16}`.
Doing so allows `pgrx` to manage either Postgres versions it installed or ones already on your
computer, and ensure that the `pgrx` managed versions don't interfere with what might already
be running. The locale of the instance is `C.UTF-8` (or equivalently, a locale of `C` with a
`ctype` of `UTF8` on macOS), or `C` if the `C.UTF-8` locale is unavailable.

`pgrx` doesn't tear down these instances. While `PGRX_HOME` is by default a hidden directory,
`pgrx` considers these important and permanent database installations.

Once started, you can connect using `psql` (if available) like so: `psql -p 28816`.
However, you probably just want the `cargo pgrx run` command.

## Compiling and Running Your Extension

```console
$ cargo pgrx run pg13
building extension with features ``
"cargo" "build" "--message-format=json-render-diagnostics"
    Finished dev [unoptimized + debuginfo] target(s) in 0.06s

installing extension
     Copying control file to /home/ana/.pgrx/13.5/pgrx-install/share/postgresql/extension/strings.control
     Copying shared library to /home/ana/.pgrx/13.5/pgrx-install/lib/postgresql/strings.so
    Building for SQL generation with features ``
    Finished dev [unoptimized + debuginfo] target(s) in 0.07s
 Discovering SQL entities
  Discovered 6 SQL entities: 0 schemas (0 unique), 6 functions, 0 types, 0 enums, 0 sqls, 0 ords, 0 hashes, 0 aggregates
     Writing SQL entities to /home/ana/.pgrx/13.5/pgrx-install/share/postgresql/extension/strings--0.1.0.sql
    Finished installing strings
    Starting Postgres v13 on port 28813
    Re-using existing database strings
psql (13.5)
Type "help" for help.

strings=# DROP EXTENSION strings;
ERROR:  extension "strings" does not exist
strings=# CREATE EXTENSION strings;
CREATE EXTENSION
strings=# \df strings.*
                                      List of functions
 Schema  |     Name      | Result data type |           Argument data types            | Type
---------+---------------+------------------+------------------------------------------+------
 strings | append        | text             | input text, extra text                   | func
 strings | return_static | text             |                                          | func
 strings | split         | text[]           | input text, pattern text                 | func
 strings | split_set     | SETOF text       | input text, pattern text                 | func
 strings | substring     | text             | input text, start integer, "end" integer | func
 strings | to_lowercase  | text             | input text                               | func
(6 rows)

strings=# select strings.to_lowercase('PGRX');
 to_lowercase
--------------
 pgrx
(1 row)
```

`cargo pgrx run <pg13 | pg14 | pg15 | pg16 | pg17>` is the primary interface into compiling and interactively testing/using your extension during development.

The very first time you execute `cargo pgrx run pgXX`, it needs to compile not only your extension, but pgrx itself, along with all its dependencies. Depending on your computer, this could take a bit of time (`pgrx` is nearly 200k lines of Rust when counting the generated bindings for Postgres). Afterwards, however (as seen in the above screenshot), it's fairly fast.

`cargo pgrx run` compiles your extension, installs it to the specified Postgres installation as described by its `pg_config` tool, starts that Postgres instance using the same process as `cargo pgrx start pgXX`, and drops you into a `psql` shell connected to a database, by default, named after your extension. From there, it's up to you to create your extension and use it.

This is also the stage where `pgrx` automatically generates the SQL schema for your extension via the `sql-generator` binary.

When you exit `psql`, the Postgres instance continues to run in the background.

For Postgres installations which are already on your computer, `cargo pgrx run` will need write permissions to the directories described by `pg_config --pkglibdir` and `pg_config --sharedir`. It's up to you to decide how to make that happen. While a single Postgres installation can be started multiple times on different ports and different data directories, it does not support multiple "extension library directories".

With the `--install-only` option, `pgrx` will compile and install your extension but will skip all subsequent steps. This might be useful if you have an alternative
testing approach and don't need an interactive shell.

```console
$ cargo pgrx run --help
Compile/install extension to a pgrx-managed Postgres instance and start psql

Usage: cargo pgrx run [OPTIONS] [PG_VERSION] [DBNAME]

Arguments:
  [PG_VERSION]  Do you want to run against pg13, pg14, pg15, pg16, pg17, or pg18? [env: PG_VERSION=]
  [DBNAME]      The database to connect to (and create if the first time).  Defaults to a database with the same name as the current extension name

Options:
  -p, --package <PACKAGE>              Package to build (see `cargo help pkgid`)
      --manifest-path <MANIFEST_PATH>  Path to Cargo.toml
  -v, --verbose...                     Enable info logs, -vv for debug, -vvv for trace
  -r, --release                        Compile for release mode (default is debug)
      --profile <PROFILE>              Specific profile to use (conflicts with `--release`)
      --all-features                   Activate all available features
      --no-default-features            Do not activate the `default` feature
  -F, --features <FEATURES>            Space-separated list of features to activate
      --pgcli                          Use an existing `pgcli` on the $PATH [env: PGRX_PGCLI=]
      --install-only                   Install without running
  -h, --help                           Print help
  -V, --version                        Print version
```

## Connect to a Database

```console
$ cargo pgrx connect
    Re-using existing database strings
psql (13.5)
Type "help" for help.

strings=# select strings.to_lowercase('PGRX');
 to_lowercase
--------------
 pgrx
(1 row)

strings=#
```

If you'd simply like to connect to a managed version of Postgres without re-compiling and installing
your extension, use `cargo pgrx connect <pg13 | pg14 | pg15 | pg16 | pg17>`.

This command will use the default database named for your extension, or you can specify another
database name as the final argument.

If the specified database doesn't exist, `cargo pgrx connect` will create it. Similarly, if
the specified version of Postgres isn't running, it'll be automatically started.

```console
$ cargo pgrx connect --help
Connect, via psql, to a Postgres instance

Usage: cargo pgrx connect [OPTIONS] [PG_VERSION] [DBNAME]

Arguments:
  [PG_VERSION]  Do you want to run against pg13, pg14, pg15, pg16, pg17, or pg18? [env: PG_VERSION=]
  [DBNAME]      The database to connect to (and create if the first time).  Defaults to a database with the same name as the current extension name [env: DBNAME=]

Options:
  -p, --package <PACKAGE>              Package to determine default `pg_version` with (see `cargo help pkgid`)
      --manifest-path <MANIFEST_PATH>  Path to Cargo.toml
  -v, --verbose...                     Enable info logs, -vv for debug, -vvv for trace
      --pgcli                          Use an existing `pgcli` on the $PATH [env: PGRX_PGCLI=]
  -h, --help                           Print help
  -V, --version                        Print version
```

## Installing Your Extension Locally

```console
$ cargo pgrx install
building extension with features ``
"cargo" "build" "--message-format=json-render-diagnostics"
    Finished dev [unoptimized + debuginfo] target(s) in 0.06s

installing extension
     Copying control file to /usr/share/postgresql/13/extension/strings.control
     Copying shared library to /usr/lib/postgresql/13/lib/strings.so
    Building for SQL generation with features ``
    Finished dev [unoptimized + debuginfo] target(s) in 0.06s
 Discovering SQL entities
  Discovered 6 SQL entities: 0 schemas (0 unique), 6 functions, 0 types, 0 enums, 0 sqls, 0 ords, 0 hashes, 0 aggregates
     Writing SQL entities to /usr/share/postgresql/13/extension/strings--0.1.0.sql
    Finished installing strings
```

If for some reason `cargo pgrx run <PG_VERSION>` isn't your style, you can use `cargo pgrx install` to install your extension
to the Postgres installation described by the `pg_config` tool currently on your `$PATH`.

You'll need write permissions to `pg_config --pkglibdir` and `pg_config --sharedir`.
If this is problematic, consider using `cargo pgrx install --sudo` which compiles the extension
as the current user and copies the extension files to their proper location using `sudo`,
prompting you for your password.

By default, `cargo pgrx install` builds your extension in debug mode. Specifying `--release` changes that.

```console
$ cargo pgrx install --help
Install the extension from the current crate to the Postgres specified by whatever `pg_config` is currently on your $PATH

Usage: cargo pgrx install [OPTIONS]

Options:
  -p, --package <PACKAGE>              Package to build (see `cargo help pkgid`)
      --manifest-path <MANIFEST_PATH>  Path to Cargo.toml
  -v, --verbose...                     Enable info logs, -vv for debug, -vvv for trace
  -r, --release                        Compile for release mode (default is debug)
      --profile <PROFILE>              Specific profile to use (conflicts with `--release`)
      --test                           Build in test mode (for `cargo pgrx test`)
  -c, --pg-config <PG_CONFIG>          The `pg_config` path (default is first in $PATH)
  -s, --sudo                           Use `sudo` to install the extension artifacts
      --all-features                   Activate all available features
      --no-default-features            Do not activate the `default` feature
  -F, --features <FEATURES>            Space-separated list of features to activate
  -h, --help                           Print help
  -V, --version                        Print version
```

## Testing with Unit Tests

```console
$ cargo pgrx test
"cargo" "test" "--features" " pg_test"
    Finished test [unoptimized + debuginfo] target(s) in 0.07s
     Running unittests (target/debug/deps/spi-312296af509607bc)

running 2 tests
building extension with features ` pg_test`
"cargo" "build" "--features" " pg_test" "--message-format=json-render-diagnostics"
    Finished dev [unoptimized + debuginfo] target(s) in 0.06s

installing extension
     Copying control file to /home/ana/.pgrx/13.5/pgrx-install/share/postgresql/extension/spi.control
     Copying shared library to /home/ana/.pgrx/13.5/pgrx-install/lib/postgresql/spi.so
    Building for SQL generation with features ` pg_test`
    Finished test [unoptimized + debuginfo] target(s) in 0.07s
 Discovering SQL entities
  Discovered 11 SQL entities: 1 schemas (1 unique), 8 functions, 0 types, 0 enums, 2 sqls, 0 ords, 0 hashes, 0 aggregates
     Writing SQL entities to /home/ana/.pgrx/13.5/pgrx-install/share/postgresql/extension/spi--0.0.0.sql
    Finished installing spi
test tests::pg_test_spi_query_by_id_direct ... ok
test tests::pg_test_spi_query_by_id_via_spi ... ok

test result: ok. 2 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 1.61s

Stopping Postgres
```

`cargo pgrx test ${VERSION}` runs your `#[test]` and `#[pg_test]` annotated functions using cargo's
test system. Note that if you request `cargo pgrx test all`, this will only run tests for versions
configured for control via cargo-pgrx AND set in your extension's Cargo.toml for the pgrx library.

During the testing process, `pgrx` starts a temporary instance of Postgres with its `PGDATA` directory in `./target/pgrx-test-data-PGVER/`. This Postgres instance is stopped as soon as the test framework has finished. The locale of the temporary instance is `C.UTF-8` (or equivalently, a locale of `C` with a `ctype` of `UTF8` on macOS), or `C` if the `C.UTF-8` locale is unavailable.

The output is standard "cargo test" output along with some Postgres log output. In the case of test failures, the failure report will include any Postgres log messages generated by that particular test.

Rust `#[test]` functions behave normally, while `#[pg_test]` functions are run **inside** the Postgres instance and have full access to all of Postgres internals. All tests are run in parallel, regardless of their type.

Additionally, a `#[pg_test]` function runs in a transaction that is aborted when the test is finished. As such, any changes it might
make to the database are not preserved.

An administrative note is that the `--runas` and `--pgdata` options can be used to control the operating-system user used
to run the separate `postmaster` process for test execution.  Likely, if `--runas` is used, then `--pgdata` will also need
to be set to a base directory that is readable and writable by that user -- the default PGDATA directory at `./target/pgrx-test-pgdata` 
will have the permissions of the user running `cargo pgrx test` and won't be chown-able to the `--runas` user.

```console
$ cargo pgrx test --help
Run the test suite for this crate

Usage: cargo pgrx test [OPTIONS] [PG_VERSION] [TESTNAME]

Arguments:
  [PG_VERSION]  Do you want to run against pg13, pg14, pg15, pg16, pg17, pg18, or all? [env: PG_VERSION=]
  [TESTNAME]    If specified, only run tests containing this string in their names

Options:
  -p, --package <PACKAGE>              Package to build (see `cargo help pkgid`)
      --manifest-path <MANIFEST_PATH>  Path to Cargo.toml
  -v, --verbose...                     Enable info logs, -vv for debug, -vvv for trace
  -r, --release                        compile for release mode (default is debug)
      --profile <PROFILE>              Specific profile to use (conflicts with `--release`)
  -n, --no-schema                      Don't regenerate the schema
      --runas <USER>                   Use `sudo` to initialize and run the Postgres test instance as this system user
      --pgdata <DIR>                   Initialize the test database cluster here, instead of the default location.  If used with `--runas`, then it must be writable by the user
      --all-features                   Activate all available features
      --no-default-features            Do not activate the `default` feature
  -F, --features <FEATURES>            Space-separated list of features to activate
  -h, --help                           Print help
  -V, --version                        Print version
```

## Benchmarking with In-Process Benches

`cargo pgrx bench ${VERSION}` runs `#[pg_bench]` benchmarks inside Postgres backends using the
same managed Postgres instance that `cargo pgrx run` uses.

Unlike `cargo pgrx test`, this is not an ephemeral test harness. Benchmarks are installed into a
managed benchmark database, by default named `$extname_benches`, and their historical results are
stored there as well.

The intended authoring model is:

```rust
#[cfg(feature = "pg_bench")]
#[pg_schema]
mod benches {
    use pgrx::prelude::*;
    use pgrx_bench::{Bencher, black_box};

    #[pg_bench]
    fn bench_hello(b: &mut Bencher) {
        b.iter(|| {
            black_box(crate::hello());
        });
    }
}
```

`cargo pgrx bench` automatically enables the `pg_bench` feature, builds in `--release` mode by
default, installs the extension artifacts using the normal install pipeline, refreshes the
extension in the benchmark database, and then runs each benchmark in-process.

Key behaviors:

- benchmark functions run inside a backend, not over an external SQL client loop
- benchmark side effects are rolled back after each benchmark invocation
- benchmark results are stored in a runner-owned `pgrx_bench` schema so history survives extension refreshes
- the CLI records environment metadata, `pg_settings`, run groups, and comparison targets
- the CLI prints the benchmark backend PID before benchmark execution starts
- the CLI prints which benchmark is currently running and the effective settings for that benchmark

You can list benchmarks and their settings with:

```console
$ cargo pgrx bench --list
bench_normalize_phrase [transaction=shared, setup=none, sample_size=100, warm_up=3000ms, measurement=5000ms, nresamples=100000, noise_threshold=0.01, significance_level=0.05]
bench_spi_insert_batch [transaction=subtransaction_per_batch, setup=prepare_spi_fixture, sample_size=50, warm_up=3000ms, measurement=2000ms, nresamples=100000, noise_threshold=0.01, significance_level=0.05]
```

To run a single benchmark:

```console
$ cargo pgrx bench pg16 bench_normalize_phrase
```

To print the backend PID and wait before benchmark execution starts, which is handy when attaching
`samply`, `perf`, `lldb`, or another external profiler/debugger:

```console
$ cargo pgrx bench --wait 10
```

`--wait` is measured in seconds, is honored after the backend PID is printed, and the CLI tells
you when it has started waiting.

To name the current run group and compare it against another group:

```console
$ cargo pgrx bench --group-name before-rewrite
$ cargo pgrx bench --group-name after-rewrite --compare-group before-rewrite
```

To emit the final summary as JSON:

```console
$ cargo pgrx bench --json
```

To render a read-only history report from the benchmark database, using the last 10 groups per
benchmark and leaving the managed Postgres instance running:

```console
$ cargo pgrx bench --report
$ cargo pgrx bench bench_normalize_phrase --report
```

`--report` starts the managed Postgres instance if needed, skips build/install/extension refresh,
reads the persisted `pgrx_bench` history, and renders colored ASCII bars. Rows marked with `*`
have broad drift from that benchmark's recorded baseline (for example profile, Postgres version,
cargo features, or nondefault `pg_settings`).

`cargo pgrx bench` is meant for in-process performance work on extension code. It is not a
replacement for `cargo pgrx test` or `cargo pgrx regress`, and it is not a client-side SQL load
testing tool.

```console
$ cargo pgrx bench --help
Usage: cargo pgrx bench [OPTIONS] [ARGS]...

Arguments:
  [ARGS]...  Positional arguments: [pgXX] [benchname] [env: PG_VERSION=]

Options:
      --dbname <DBNAME>
          If specified, use this database name instead of `$extname_benches`
      --group-name <GROUP_NAME>
          Unique name for this benchmark group
  -v, --verbose...
          Enable info logs, -vv for debug, -vvv for trace
      --compare-group <COMPARE_GROUP>
          Named benchmark group to compare against
      --resetdb
          Recreate the benchmark database before running
      --cascade
          Use CASCADE when dropping the extension during refresh
      --list
          List discovered benchmark wrappers and exit
      --report
          Render a read-only history report from the benchmark database
      --json
          Emit the final summary as JSON
      --wait <SECONDS>
          Sleep for this many seconds after printing the backend PID and before starting benchmarks
          [default: 0]
  -p, --package <PACKAGE>
          Package to build (see `cargo help pkgid`)
      --manifest-path <MANIFEST_PATH>
          Path to Cargo.toml
      --debug
          Compile for debug mode instead of the default release mode
      --profile <PROFILE>
          Specific profile to use (conflicts with `--debug`)
      --all-features
          Activate all available features
      --no-default-features
          Do not activate the `default` feature
  -F, --features <FEATURES>
          Space-separated list of features to activate
      --target <TARGET>
      --postgresql-conf <POSTGRESQL_CONF>
          Custom `postgresql.conf` settings in the form of `key=value`
  -h, --help
          Print help
  -V, --version
          Print version
```

## Testing with Regression Tests

pgrx supports a regression test system very similar to the one prescribed by Postgres' `pg_regress` tool.  In fact, pgrx uses `pg_regress` to run the regression tests.

`cargo pgrx regress` is used to run the regression tests.  It has a number of options similar to `cargo pgrx test`.

Key flags:

| Flag | Purpose                                                                                      |
|------|----------------------------------------------------------------------------------------------|
| `--resetdb` | Drop and recreate the test database (recommended for reproducible runs)                      |
| `--add <name>` | Bootstrap a new test — implies `--resetdb`, runs `setup.sql`, promotes output to `expected/` |
| `-t` / `--test-filter <string>` | Only run tests whose names contain this string                                               |
| `-a` / `--auto` | Overwrite expected output for failed tests with actual output                                |
| `-v` / `--verbose` | Print regression diffs to stderr on failure                                                  |
| `--dry-run` | Print what would happen without doing it                                                     |
| `--repeat <N>` | Run the entire configuration N times (default: 1)                                            |
| `-p` / `--package <name>` | Package to build (auto-detected in workspaces with a single pgrx extension)                  |
| `[PG_VERSION]` | Postgres version (e.g., `pg18`). Optional — defaults to Cargo.toml's default feature         |

Regression tests are split into `*.sql` files and `*.out` files.  The files themselves are organized into separate directories rooted at `./tests/pg_regress`.

For example, using our [range example](../pgrx-examples/range/), the directory structure looks like this:

```console
$ tree       
.
├── Cargo.lock
├── Cargo.toml
├── README.md
├── tests
│   └── pg_regress
│       ├── sql                    # these are the individual regression test scripts
│       │   ├── make_range.sql
│       │   ├── setup.sql
│       │   └── store_ranges.sql
│       └── expected               # these are the corresponding test output files
│           ├── make_range.out
│           ├── setup.out
│           └── store_ranges.out
├── range.control
└── src
    └── lib.rs
```

`setup.sql` is a special test in that it's run first, by itself, whenever the test database is first created, or reset using the `--resetdb` argument.

When creating a new test, first make the `.sql` file in `./tests/pg_regress/sql/` and then use `--add` to bootstrap it:

```console
$ echo "SELECT 1;" > ./tests/pg_regress/sql/example.sql
$ cargo pgrx regress --add example
```

This will:
1. Drop and recreate the test database (`--add` implies `--resetdb`)
2. Run `setup.sql` to establish schema/data
3. Run the new test
4. Copy its output to `./tests/pg_regress/expected/example.out`
5. `git add` the new expected output file

After bootstrapping, verify the test passes by running the full suite:

```console
$ cargo pgrx regress --resetdb
```

Tests without expected output are **skipped** by default during normal runs. If you use `--test-filter` to target a test that hasn't been bootstrapped yet, you'll get a clear error directing you to use `--add` first.

#### Updating expected output after code changes

When code changes alter query output or EXPLAIN plans, use `--auto` to accept the new output:

```console
$ cargo pgrx regress --auto --resetdb
```

`--auto` overwrites expected output for **failed** tests with their actual output. Review the changes with `git diff` to verify they match the intended behavioral change. **Never manually edit `.out` files** — always let `--auto` generate them.

When tests fail, the path to the `regression.diffs` file is always shown. Use `-v` to also print the full diff content inline.

### Things to Know

- `setup.sql` is only executed when tests are run for the first time, or the `--resetdb` argument is used.  This includes when running a single test with `--resetdb` — `setup.sql` will always run first to establish the database schema and data before the filtered test executes.
- The point of `setup.sql` is to perform some heavy-weight database object creation/data-loading _only_ when the test regression database is created.
- tests are executed in alphabetical order
- `.sql` files without a corresponding `expected/*.out` file are **skipped** during normal runs — use `--add <name>` to bootstrap new tests
- pgrx creates a database named `$extname_regress` unless `--dbname` is used
- Postgres' documentation for `pg_regress` [begins here](https://www.postgresql.org/docs/current/regress.html).  While pgrx does not support every knob and dial, its organization is largely compatible (PRs welcome to enhance features)
- to regenerate the expected test output, delete the `./tests/pg_regress/expected/TEST_NAME.out` file and re-add it with `cargo pgrx regress --add TEST_NAME`
- `pg_regress` uses `psql` to run each test and literally diffs the output against the expected output file.  pgrx does two things to help eliminate noise in the test output.  The first is it sets `client_min_messages=warning` when starting the Postgres instance and it also passes `-v VERBOSITY=terse` through to `psql`.
- In a workspace with a single pgrx extension crate, `--package` is auto-detected — no need to specify it manually

### Diffing `psql` Output?

Yes, Postgres' `pg_regress` tool plays each `test_name.sql` file through `psql`, captures the full output, and diffs that output against the test's corresponding `test_name.out` file.  It's a good idea for your tests to avoid variability in their output.

Avoiding variability can mean some simple things like

- ensuring the results of SELECT statements that return multiple rows are always sorted in a predictable/repeatable manner
- avoiding outputting "random" values such as the result of the `random()` function, `txid_current()`, and others
    
    
### Be Kind to Yourself

While always good advice, in the context of individual regression tests, this means that a test should not leave anything behind or should be able to tolerate leftover database objects from previous runs.  

Each test should be written to "DROP IF EXISTS ... ; CREATE ..." or instead "DROP ..." every database object at the end of the test.

You can ignore this advice if you **always** run the regression tests with `--resetdb` as you'll have a clean database each time

An example of an unkind test might be:

```console
$ echo "CREATE TABLE foo();" > ./tests/pg_regress/sql/bad.sql
$ cargo pgrx regress --add bad
```

Then on a second run (without `--resetdb`), the `CREATE TABLE` will fail because the table already exists. You'd see the regression diffs printed inline:

```console
$ diff -U3  ~/_work/pgrx/tests/pgrx-examples/range/tests/pg_regress/expected/bad.out  ~/_work/pgrx/tests/pgrx-examples/range/tests/pg_regress/results/bad.out
---  ~/_work/pgrx/tests/pgrx-examples/range/tests/pg_regress/expected/bad.out	2025-05-07 12:15:10.759010127 -0400
+++  ~/_work/pgrx/tests/pgrx-examples/range/tests/pg_regress/results/bad.out	2025-05-07 12:15:10.775009912 -0400
@@ -1 +1,2 @@
 CREATE TABLE foo();
+ERROR:  relation "foo" already exists
```

What you wanted in the .sql file is either

```sql
DROP TABLE IF EXISTS foo;  
CREATE TABLE foo();
``` 

or 

```sql
CREATE TABLE foo(); 
-- ... 
DROP TABLE foo;
```

Either be resilient in the face of existing objects or cleanup when the test is finished.


## Building an Installation Package

```console
$ cargo pgrx package
building extension with features ``
"cargo" "build" "--release" "--message-format=json-render-diagnostics"
    Finished release [optimized] target(s) in 0.07s

installing extension
     Copying control file to target/release/spi-pg13/usr/share/postgresql/13/extension/spi.control
     Copying shared library to target/release/spi-pg13/usr/lib/postgresql/13/lib/spi.so
    Building for SQL generation with features ``
    Finished release [optimized] target(s) in 0.07s
 Discovering SQL entities
  Discovered 8 SQL entities: 0 schemas (0 unique), 6 functions, 0 types, 0 enums, 2 sqls, 0 ords, 0 hashes, 0 aggregates
     Writing SQL entities to target/release/spi-pg13/usr/share/postgresql/13/extension/spi--0.0.0.sql
    Finished installing spi
```

`cargo pgrx package [--debug]` builds your extension, in `--release` mode, to a directory structure in
`./target/[debug | release]/extension_name-PGVER` using the Postgres installation path information from the `pg_config`
tool on your `$PATH`.

The intent is that you'd then change into that directory and build a tarball or a .deb or .rpm package.

The directory structure `cargo pgrx package` creates starts at the root of the filesystem, as a package-manager installed
version of Postgres is likely to split `pg_config --pkglibdir` and `pg_config --sharedir` into different base paths.

(In the example screenshot above, `cargo pgrx package` was used to build a directory structure using my manually installed
version of Postgres 12.)

This command could be useful from Dockerfiles, for example, to automate building installation packages for various Linux
distributions or MacOS Postgres installations.

```console
$ cargo pgrx package --help
Create an installation package directory

Usage: cargo pgrx package [OPTIONS]

Options:
  -p, --package <PACKAGE>              Package to build (see `cargo help pkgid`)
      --manifest-path <MANIFEST_PATH>  Path to Cargo.toml
  -v, --verbose...                     Enable info logs, -vv for debug, -vvv for trace
  -d, --debug                          Compile for debug mode (default is release)
      --profile <PROFILE>              Specific profile to use (conflicts with `--debug`)
      --test                           Build in test mode (for `cargo pgrx test`)
  -c, --pg-config <PG_CONFIG>          The `pg_config` path (default is first in $PATH)
      --out-dir <OUT_DIR>              The directory to output the package (default is `./target/[debug|release]/extname-pgXX/`)
      --all-features                   Activate all available features
      --no-default-features            Do not activate the `default` feature
  -F, --features <FEATURES>            Space-separated list of features to activate
  -h, --help                           Print help
  -V, --version                        Print version
```

## Inspect your Extension Schema

If you just want to look at the full extension schema that pgrx will generate, use
`cargo pgrx schema`.

```console
$ cargo pgrx schema --help
Generate extension schema files

Usage: cargo pgrx schema [OPTIONS] [ARGS]...

Arguments:
  [ARGS]...  First arg may be a PostgreSQL version label (`pg13`..`pg18`).
             Remaining args are SQL item names to emit (functions, types,
             enums, operators, aggregates, triggers, schemas, extension_sql
             blocks). When item names are given, only those items and their
             transitive dependencies are emitted, in install order, and
             'MODULE_PATHNAME' is substituted with '$libdir/<lib_name>' so
             the output can be replayed directly. Names containing `::` are
             matched as Rust paths to disambiguate.

Options:
  -p, --package <PACKAGE>              Package to build (see `cargo help pkgid`)
      --manifest-path <MANIFEST_PATH>  Path to Cargo.toml
  -v, --verbose...                     Enable info logs, -vv for debug, -vvv for trace
      --test                           Build in test mode (for `cargo pgrx test`)
  -r, --release                        Compile for release mode (default is debug)
      --profile <PROFILE>              Specific profile to use (conflicts with `--release`)
  -c, --pg-config <PG_CONFIG>          The `pg_config` path (default is first in $PATH)
      --all-features                   Activate all available features
      --no-default-features            Do not activate the `default` feature
  -F, --features <FEATURES>            Space-separated list of features to activate
  -o, --out <OUT>                      A path to output a produced SQL file (default is `stdout`)
  -d, --dot <DOT>                      A path to output a produced GraphViz DOT file
      --skip-build                     Skip building a fresh extension shared object
      --no-alter-extension             Don't emit `ALTER EXTENSION ... ADD ...` statements when
                                       extracting specific items (see "Attaching Slices" below)
  -h, --help                           Print help
  -V, --version                        Print version
```

### Emitting a Slice of the Schema

Any positional arguments after an optional `pgXX` version label are treated as SQL item
names. The output is restricted to those items plus every dependency they need, in
install order. Names match against each entity's SQL-visible identifier: `name` for
functions, types, enums, aggregates, and schemas; `opname` for operators (for example
`===`); `function_name` for triggers; and `name` for `extension_sql!` blocks. A name
containing `::` is treated as a Rust path and matched against `full_path`, which is the
way to disambiguate collisions (for example two functions named `dup_fn` in different
modules).

When item names are supplied, every occurrence of `'MODULE_PATHNAME'` in the generated
SQL is substituted with `'$libdir/<lib_name>'`, so the output can be replayed directly
into a database without relying on the extension's control file to resolve
`MODULE_PATHNAME`.

```shell
# Emit one function and its dependencies, with MODULE_PATHNAME substituted
cargo pgrx schema my_function

# Multiple items at once
cargo pgrx schema my_function MyType ===

# Specify a Postgres version first, then the items
cargo pgrx schema pg18 my_function MyType

# Disambiguate with a Rust path when the bare name matches multiple items
cargo pgrx schema my_crate::submodule::dup_fn

# Write the slice to a file instead of stdout (combines with item selection)
cargo pgrx schema --out /tmp/extracted_schema_objects.sql my_function MyType
```

#### Attaching Slices to an Already-Installed Extension

When item names are supplied, the emitted slice is wrapped in `BEGIN;`/`COMMIT;`
and every created object is followed by an `ALTER EXTENSION "<ext>" ADD ...`
statement. Piping the output into a database where the extension is already
installed makes the new objects members of the extension, verifiable via
`pg_depend`.

```shell
# Add a new function to an already-installed extension
cargo pgrx schema my_new_fn | cargo pgrx connect
```

Pass `--no-alter-extension` to opt out of this (for example, to generate SQL
for hand-editing or to match the pre-feature output).

`extension_sql!()` blocks that don't declare `creates = [...]` cannot be
attached automatically; the emitter prints a warning to stderr naming the
block's `file:line` so the user knows which objects to attach by hand.


## Extension Version Upgrade Scripts

When creating a pgrx extension using `cargo pgrx new foo`, the new extension template directory tree includes a 
directory named `./sql`:

```shell
$ tree
.
├── Cargo.toml
├── blah.control
├── sql
└── src
    └── lib.rs

2 directories, 3 files
```

It is in this directory that you would **manually** create extension version upgrade scripts.  The files you create should
be named in the manner prescribed by the [Postgres Extension Updates documentation](https://www.postgresql.org/docs/current/extend-extensions.html#EXTEND-EXTENSIONS-UPDATES).
Generally that format is `foo--oldver--newver.sql`.  For example, `foo--1.0.0--1.0.1.sql`.  

When a user runs `ALTER EXTENSION foo UPDATE;` in a database with the `foo` extension, Postgres will build a graph of
upgrade scripts to run, starting with the currently installed version and ending with the `default_version` defined in
the extensions `.control` file.  Postgres will then execute the scripts along the shortest path.

It is your responsibility to hand-write these extension upgrade scripts in whatever manner would allow Postgres to update
your extension from one version to the next.  pgrx has no ability to auto-generate these scripts.

While pgrx does not generate these upgrade scripts, it does know about them and all pgrx commands (`cargo pgrx test/run/install/package/bench`) 
that generate extension artifacts will automatically copy these files, and only these files, from the `./sql` directory 
to their final destination as dictated by `pg_config`.


## Information about pgrx-managed development environment

```console
$ cargo pgrx info --help
Provides information about pgrx-managed development environment

Usage: cargo pgrx info [OPTIONS] <COMMAND>

Commands:
  path       Print path to a base version of Postgres build
  pg-config  Print path to pg_config for a base version of Postgres
  version    Print specific version for a base Postgres version
  help       Print this message or the help of the given subcommand(s)

Options:
  -v, --verbose...  Enable info logs, -vv for debug, -vvv for trace
  -h, --help        Print help
  -V, --version     Print version
```

`cargo pgrx info` helps retrieving information about pgrx-managed development
environment (such as managed Postgres installations)

## EXPERIMENTAL: Versioned shared-object support

`pgrx` experimentally supports the option to produce a versioned shared library. This allows multiple versions of the
extension to be installed side-by-side, and can enable the deprecation (and removal) of functions between extension
versions. There are some caveats which must be observed when using this functionality. For this reason it is currently
experimental.

### Activation

Versioned shared-object support is enabled by removing the `module_pathname` configuration value in the extension's
`.control` file.

### Concepts

Postgres has the implicit requirement that C extensions maintain ABI compatibility between versions. The idea behind
this feature is to allow interoperability between two versions of an extension when the new version is not ABI
compatible with the old version.

The mechanism of operation is to version the name of the shared library file, and to hard-code function definitions to
point to the versioned shared library file. Without versioned shared-object support, the SQL definition of a C function
would look as follows:

```SQL
CREATE OR REPLACE FUNCTION "hello_extension"() RETURNS text /* &str */
STRICT
LANGUAGE c /* Rust */
AS 'MODULE_PATHNAME', 'hello_extension_wrapper';
```

`MODULE_PATHNAME` is replaced by Postgres with the configured value in the `.control` file. For pgrx-based extensions,
this is  usually set to `<extension-name>`.

When using versioned shared-object support, the same SQL would look as follows:

```SQL
CREATE OR REPLACE FUNCTION "hello_extension"() RETURNS text /* &str */
STRICT
LANGUAGE c /* Rust */
AS 'extension-0.0.0', 'hello_extension_wrapper';
```

Note that the versioned shared library is hard-coded in the function definition. This corresponds to the
`extension-0.0.0.so` file which `pgrx` generates.

It is important to note that the emitted SQL is version-dependent. This means that all previously-defined C functions
must be redefined to point to the current versioned-so in the version upgrade script. As an example, when updating the
extension version to 0.1.0, the shared object will be named `<extension-name>-0.1.0.so`, and `cargo pgrx schema` will
produce the following SQL for the above function:

```SQL
CREATE OR REPLACE FUNCTION "hello_extension"() RETURNS text /* &str */
STRICT
LANGUAGE c /* Rust */
AS 'extension-0.1.0', 'hello_extension_wrapper';
```

This SQL must be used in the upgrade script from `0.0.0` to `0.1.0` in order to point the `hello_extension` function to
the new shared object. `pgrx` _does not_ do any magic to determine in which version a function was introduced or modified
and only place it in the corresponding versioned so file. By extension, you can always expect that the shared library
will contain _all_ functions which are still defined in the extension's source code.

This feature is not designed to assist in the backwards compatibility of data types.

### `@MODULE_PATHNAME@` Templating

In case you are already providing custom SQL definitions for Rust functions, you can use the `@MODULE_PATHNAME@`
template in your custom SQL. This value will be replaced with the path to the actual shared object.

The following example illustrates how this works:

```rust
#[pg_extern(sql = r#"
    CREATE OR REPLACE FUNCTION tests."overridden_sql_with_fn_name"() RETURNS void
    STRICT
    LANGUAGE c /* Rust */
    AS '@MODULE_PATHNAME@', '@FUNCTION_NAME@';
"#)]
fn overridden_sql_with_fn_name() -> bool {
    true
}
```

### Caveats

There are some scenarios which are entirely incompatible with this feature, because they rely on some global state in
Postgres, so loading two versions of the shared library will cause trouble.

These scenarios are:
- when using shared memory
- when using query planner hooks
