`pg_parquet` is an open source project primarily authored and
maintained by the team at Crunchy Data. All contributions are welcome. The pg_parquet uses the PostgreSQL license and does not require any contributor agreement to submit patches.

Our contributors try to follow good software development practices to help
ensure that the code that we ship to our users is stable. If you wish to
contribute to the pg_parquet in any way, please follow the guidelines below.

Thanks! We look forward to your contribution.

# General Contributing Guidelines

All ongoing development for an upcoming release gets committed to the
**`main`** branch. The `main` branch technically serves as the "development"
branch as well, but all code that is committed to the `main` branch should be
considered _stable_, even if it is part of an ongoing release cycle.

- Ensure any changes are clear and well-documented:
  - The most helpful code comments explain why, establish context, or efficiently summarize how. Avoid simply repeating details from declarations. When in doubt, favor overexplaining to underexplaining.
  - Do not submit commented-out code. If the code does not need to be used
anymore, please remove it.
  - While `TODO` comments are frowned upon, every now and then it is ok to put a `TODO` to note that a particular section of code needs to be worked on in the future. However, it is also known that "TODOs" often do not get worked on, and as such, it is more likely you will be asked to complete the TODO at the time you submit it.
- Make sure to add tests which cover the code lines that are introduced by your changes. See [testing](#testing).
- Make sure to [format and lint](#format-and-lint) the code.
- Ensure your commits are atomic. Each commit tells a story about what changes
are being made. This makes it easier to identify when a bug is introduced into
the codebase, and as such makes it easier to fix.
- All commits must either be rebased in atomic order or squashed (if the squashed
commit is considered atomic). Merge commits are not accepted. All conflicts must be resolved prior to pushing changes.
- **All pull requests should be made from the `main` branch.**

# Commit Messages

Commit messages should be as descriptive and should follow the general format:

```
A one-sentence summary of what the commit is.

Further details of the commit messages go in here. Try to be as descriptive of
possible as to what the changes are. Good things to include:

- What the changes is.
- Why the change was made.
- What to expect now that the change is in place.
- Any advice that can be helpful if someone needs to review this commit and
understand.
```

If you wish to tag a GitHub issue or another project management tracker, please
do so at the bottom of the commit message, and make it clearly labeled like so:

```
Issue: CrunchyData/pg_parquet#12
```

# Submitting Pull Requests

All work should be made in your own repository fork. When you believe your work
is ready to be committed.

## Upcoming Features

Ongoing work for new features should occur in branches off of the `main`
branch.

# Start Local Environment

There are 2 ways to start your local environment to start contributing pg_parquet.
- [Installation From Source](#installation-from-source)
- [Devcontainer](#devcontainer)

## Installation From Source

See [README.md](README.md#installation-from-source).

## Devcontainer

If you want to work on a totally ready-to-work container environment, you can try our
[devcontainer](.devcontainer/devcontainer.json). If you have chance to work on
[vscode editor](https://code.visualstudio.com), you can start pg_parquet project
inside the devcontainer. Please see [how you start the devcontainer](https://code.visualstudio.com/docs/devcontainers/containers).

# Postgres Support Matrix

You can see the current supported Postgres versions from [README.md](README.md#postgres-support-matrix).
Supported Postgres versions exist as Cargo feature flags at [Cargo.toml](Cargo.toml).
By default, pg_parquet is built with the latest supported Postgres version flag enabled.
If you want to build pg_parquet against another Postgres version, you can do it
by specifying the feature flag explicitly like `cargo pgrx build --features "pg16"`.
The same applies for running a session via `cargo pgrx run pg16` or
testing via `cargo pgrx test pg16`.

# Testing

We run `RUST_TEST_THREADS=1 cargo pgrx test` to run all our unit tests. If you
run a specific test, you can do it via regex patterns like below:
`cargo pgrx run pg17 test_numeric_*`.

> [!WARNING]
> Make sure to pass RUST_TEST_THREADS=1 as environment variable to `cargo pgrx test`.

Object storage tests are integration tests which require a storage emulator running
locally. See [ci.yml](.github/workflows/ci.yml) to see how local storage emulators
are started. You can also see the required environment variables from
[.env file](.devcontainer/.env).

# Format and Lint

We use `cargo-fmt` as formatter and `cargo-clippy` as linter. You can check
how we run them from [ci.yml](.github/workflows/ci.yml).


# Release

We apply semantic versioning for our releases. We do not support long term release branches (backporting) yet.
The release process is as follows:

1. Open PR to start release preparation,
2. Bump the package version at `Cargo.toml` file
3. Upgrade dependencies via `cargo update`
4. Use a schema diff tool, if possible (or manually), to generate
  - sql upgrade file from previous release to the current release `pg_parquet--<prev-version>-<next-version>.sql`
  - sql file of the current schema `pg_parquet.sql`
5. Merge the PR into main
6. Tag the latest commit with naming convention of `v<major>.<minor>.<patch>`
7. Release it with important and breaking (if any) changes
