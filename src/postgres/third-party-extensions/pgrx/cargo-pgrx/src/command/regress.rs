//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use crate::CommandExecute;
use crate::command::get::get_property;
use crate::command::run::Run;
use crate::command::start::collect_postgresql_conf_settings;
use crate::manifest::get_package_manifest;
use owo_colors::OwoColorize;
use pgrx_pg_config::{PgConfig, createdb, dropdb, is_supported_major_version};
use std::collections::HashSet;
use std::env::temp_dir;
use std::fs::{DirEntry, File};
use std::io::{BufRead, BufReader, Write};
use std::path::{Path, PathBuf};
use std::process::{Command, ExitStatus, Stdio};

/// Run the regression test suite for this crate
#[derive(clap::Args, Debug, Clone)]
#[clap(author)]
pub(crate) struct Regress {
    /// Positional arguments: [pgXX] [testname]
    ///
    /// `cargo pgrx regress` — run all tests against the default pg version
    /// `cargo pgrx regress <testname>` — run a specific test against the default pg version
    /// `cargo pgrx regress pgXX` — run all tests against the specified pg version
    /// `cargo pgrx regress pgXX <testname>` — run a specific test against the specified pg version
    #[clap(env = "PG_VERSION")]
    pub(crate) args: Vec<String>,

    /// Resolved pg_version (not a CLI arg)
    #[clap(skip)]
    pub(crate) pg_version: Option<String>,

    /// Resolved test_filter (not a CLI arg)
    #[clap(skip)]
    pub(crate) test_filter: Option<String>,

    /// If specified, use this database name instead of the auto-generated version of `$extname_regress`
    #[clap(long)]
    pub(crate) dbname: Option<String>,

    /// Recreate the test database, even if it already exists
    #[clap(long)]
    pub(crate) resetdb: bool,
    /// Package to build (see `cargo help pkgid`)
    #[clap(long, short)]
    pub(crate) package: Option<String>,
    /// Path to Cargo.toml
    #[clap(long, value_parser)]
    pub(crate) manifest_path: Option<PathBuf>,
    /// compile for release mode (default is debug)
    #[clap(long, short)]
    pub(crate) release: bool,
    /// Specific profile to use (conflicts with `--release`)
    #[clap(long)]
    pub(crate) profile: Option<String>,
    /// Don't regenerate the schema
    #[clap(long, short)]
    pub(crate) no_schema: bool,
    /// Use `sudo` to initialize and run the Postgres test instance as this system user
    #[clap(long, value_name = "USER")]
    pub(crate) runas: Option<String>,
    /// Initialize the test database cluster here, instead of the default location.  If used with `--runas`, then it must be writable by the user
    #[clap(long, value_name = "DIR")]
    pub(crate) pgdata: Option<PathBuf>,
    #[clap(flatten)]
    pub(crate) features: clap_cargo::Features,
    #[clap(from_global, action = clap::ArgAction::Count)]
    pub(crate) verbose: u8,
    /// verbosity of error reports: default, verbose, terse, or sqlstate
    #[clap(long, value_name = "VERBOSITY")]
    pub(crate) psql_verbosity: Option<String>,

    /// Custom `postgresql.conf` settings in the form of `key=value`, ie `log_min_messages=debug1`
    #[clap(long)]
    pub(crate) postgresql_conf: Vec<String>,

    /// Overwrite expected output for failed tests with actual output
    #[clap(long, short)]
    pub(crate) auto: bool,

    /// Bootstrap a new test: run it, promote its output to expected/, and exit.
    /// Implies --resetdb. The test's setup.sql is run first.
    #[clap(long, value_name = "TESTNAME")]
    pub(crate) add: Option<String>,

    /// Print what would happen without doing it
    #[clap(long)]
    pub(crate) dry_run: bool,

    /// Run the test suite this many times (default: 1)
    #[clap(long, default_value_t = 1, value_name = "N")]
    pub(crate) repeat: u32,

    /// Run Postgres under valgrind while executing the regression tests
    #[clap(long)]
    pub(crate) valgrind: bool,
}

impl Regress {
    #[rustfmt::skip]
    fn is_setup_sql_newer(&self, manifest_path: &Path) -> bool {

        // if we have reset the db, then re-run the setup
        if self.resetdb {
            return true;
        }

        let sql = manifest_path_to_sql_tests_path(manifest_path);
        if !sql.exists() { return false; }
        let expected = manifest_path_to_expected_tests_output_path(manifest_path);
        if !expected.exists() {return false; }

        let setup_sql = sql.join("setup.sql");
        let setup_out = expected.join("setup.out");

        let Ok(setup_sql) = std::fs::metadata(setup_sql) else { return false; };
        let Ok(setup_out) = std::fs::metadata(setup_out) else { return true; }; // there is no output file, so setup.sql is definitely newer

        let Ok(sql_modified) = setup_sql.modified() else { return false; };
        let Ok(out_modified) = setup_out.modified() else { return false; };

        sql_modified > out_modified
    }

    fn list_sql_tests(
        &self,
        manifest_path: &Path,
        include_setup: bool,
    ) -> eyre::Result<Vec<DirEntry>> {
        let sql = manifest_path_to_sql_tests_path(manifest_path);
        if !sql.exists() {
            std::fs::create_dir(&sql)?;
        }
        let mut files = std::fs::read_dir(sql)?.collect::<Result<Vec<_>, _>>()?;

        Self::organize_files(&mut files, "sql", include_setup);
        Ok(files)
    }

    fn list_expected_outputs(
        &self,
        manifest_path: &Path,
        include_setup: bool,
    ) -> eyre::Result<Vec<DirEntry>> {
        let expected = manifest_path_to_expected_tests_output_path(manifest_path);
        if !expected.exists() {
            std::fs::create_dir(&expected)?;
        }
        let mut files = std::fs::read_dir(expected)?.collect::<Result<Vec<_>, _>>()?;

        Self::organize_files(&mut files, "out", include_setup);

        Ok(files)
    }

    fn list_results_outputs(
        &self,
        manifest_path: &Path,
        include_setup: bool,
    ) -> eyre::Result<Vec<DirEntry>> {
        let results = manifest_path_to_results_output_path(manifest_path);
        if !results.exists() {
            std::fs::create_dir(&results)?;
        }
        let mut files = std::fs::read_dir(results)?.collect::<Result<Vec<_>, _>>()?;

        Self::organize_files(&mut files, "out", include_setup);

        Ok(files)
    }

    fn organize_files(files: &mut Vec<DirEntry>, only: &str, include_setup: bool) {
        // remove any files that don't have `only` as the extension
        files.retain(|entry| {
            entry
                .metadata()
                .map(|metadata| {
                    metadata.is_file()
                        && entry
                            .file_name()
                            .to_str()
                            .map(|filename| filename.ends_with(&format!(".{only}")))
                            .unwrap_or_default()
                })
                .unwrap_or_default()
        });

        // `setup.{only}` is a special file that we handle separately
        let is_setup = |entry: &DirEntry| {
            entry
                .file_name()
                .to_str()
                .is_some_and(|filename| filename.ends_with(&format!("setup.{only}")))
        };

        // remove the "setup" file from the list
        let setup_entry = files.iter().position(is_setup).map(|idx| files.remove(idx));

        // not all filesystems list directories sorted and we want some kind of guaranteed evaluation order
        files.sort_unstable_by_key(|entry| entry.file_name());

        // if we detected a setup file and the caller wants to include it, make it the first entry
        if let Some(setup_entry) = setup_entry
            && include_setup
        {
            files.insert(0, setup_entry);
        }
    }

    /// Bootstrap a new test by running it via pg_regress, promoting its output
    /// to `expected/`, and git-adding the new file.
    fn bootstrap_new_test(
        &self,
        pg_config: &PgConfig,
        manifest_path: &Path,
        pgregress_path: &Path,
        dbname: &str,
        test_file: &DirEntry,
    ) -> eyre::Result<()> {
        let test_name = make_test_name(test_file);
        let verbosity = &self.psql_verbosity.clone().unwrap_or("terse".into());

        println!("{} new test `{}`", "Bootstrapping".bold().green(), test_name.bold().cyan());

        if let Some(test_result_output) = create_regress_output(
            pg_config,
            manifest_path,
            pgregress_path,
            dbname,
            test_file,
            verbosity,
        )? {
            let expected_path = manifest_path_to_expected_tests_output_path(manifest_path)
                .join(format!("{test_name}.out"));

            println!(
                "{} test output to {}",
                "     Copying".bold().green(),
                expected_path.display().bold().cyan()
            );
            std::fs::copy(&test_result_output, &expected_path)?;
            add_to_git(&expected_path)?;
        }

        Ok(())
    }

    /// Returns `Ok(true)` when all tests passed, `Ok(false)` when at least one failed.
    fn run_all_tests(
        &self,
        pg_config: &PgConfig,
        manifest_path: &Path,
        pgregress_path: &Path,
        dbname: &str,
        test_files: &[&DirEntry],
        output_files: &[&DirEntry],
        include_setup: bool,
        run: u32,
    ) -> eyre::Result<bool> {
        let output_names = output_files.iter().map(|e| make_test_name(e)).collect::<HashSet<_>>();

        // Separate tests into those with expected output and those without
        let (ready_tests, new_tests): (Vec<&&DirEntry>, Vec<&&DirEntry>) =
            test_files.iter().partition(|entry| output_names.contains(&make_test_name(entry)));
        let ready_tests: Vec<&DirEntry> = ready_tests.into_iter().copied().collect();

        // Report skipped tests in the same style as PASS/FAIL
        for new_test in &new_tests {
            let name = make_test_name(new_test);
            println!(
                "{} {} (use {} to bootstrap)",
                "SKIP".bold().yellow(),
                name,
                "--add".bold().white()
            );
        }
        let skipped_cnt = new_tests.len();

        if ready_tests.is_empty() {
            println!("passed=0 failed=0 skipped={skipped_cnt}");
            return Ok(true);
        }

        // The default verbosity is terse in order to avoid verbose log output
        // being enshrined in expected test output
        let verbosity = &self.psql_verbosity.clone().unwrap_or("terse".into());

        // Run all tests that have expected output
        let success =
            run_tests(pg_config, pgregress_path, dbname, &ready_tests, verbosity, skipped_cnt)?;

        if !success {
            // Show the regression diffs path (always) and content (with -v).
            // When repeating, rename to regression.<run>.diffs so each attempt is preserved.
            print_regression_diffs(manifest_path, self.verbose, run, self.repeat);

            if self.auto {
                // Promote actual output to expected for failed tests
                let results_files = self.list_results_outputs(manifest_path, include_setup)?;

                println!();
                for entry in results_files {
                    let filename = entry
                        .file_name()
                        .to_str()
                        .expect("filename should be valid UTF8")
                        .to_owned();
                    let expected_path =
                        manifest_path_to_expected_tests_output_path(manifest_path).join(filename);

                    if !expected_path.exists() {
                        continue;
                    }

                    let src = std::fs::read_to_string(entry.path())?;
                    let dst = std::fs::read_to_string(&expected_path)?;
                    if src != dst {
                        println!(
                            "{} {}'s output to {}",
                            "   Promoting".bold().yellow(),
                            make_test_name(&entry).bold().bright_red(),
                            expected_path.display().bold().cyan()
                        );
                        std::fs::copy(entry.path(), &expected_path)?;
                    }
                }
            }
        }

        Ok(success)
    }
}

impl CommandExecute for Regress {
    #[tracing::instrument(level = "error", skip(self))]
    fn execute(mut self) -> eyre::Result<()> {
        unsafe {
            std::env::set_var("PGRX_REGRESS_TESTING", "1");
        }

        // Resolve positional args into pg_version and test_filter.
        // If the first arg looks like a pg version (pgNN), it's the pg version;
        // otherwise it's a test name filter.
        self.resolve_args();

        // --add implies --resetdb
        if self.add.is_some() {
            self.resetdb = true;
        }

        let (_, manifest_path) = get_package_manifest(
            &self.features,
            self.package.as_deref(),
            self.manifest_path.as_deref(),
        )?;
        let extname = get_property(&manifest_path, "extname")?
            .expect("extension name property `extname` should always be known");
        self.dbname = Some(self.dbname.unwrap_or_else(|| format!("{extname}_regress")));

        // we purposely want as little noise as possible to end up in the expected test output files
        self.postgresql_conf.push("client_min_messages=warning".into());
        let postgresql_conf = collect_postgresql_conf_settings(&self.postgresql_conf)?;

        // --dry-run: resolve everything but don't execute
        if self.dry_run {
            return self.execute_dry_run(&manifest_path);
        }

        // install the extension
        let (pg_config, dbname) = Run::from(&self).install(false, &postgresql_conf)?;
        let pgregress_path = pg_config.pg_regress_path()?;

        let mut any_failed = false;

        for run in 1..=self.repeat {
            if self.repeat > 1 {
                println!();
                println!("=== run {run} of {} ===", self.repeat);
            }

            if self.is_setup_sql_newer(&manifest_path) {
                println!(
                    "{} database {} to be (re)created as `setup.sql` is newer than its expected output",
                    "     Forcing".bold().yellow(),
                    dbname.cyan()
                );
            }

            // NB:  the `is_test` argument for both `dropdb()` and `createdb()` is for `cargo pgrx test`,
            // which creates its own Postgres instance and has its own port and datadir and such, so we
            // say `false` here.
            if self.resetdb || self.is_setup_sql_newer(&manifest_path) {
                dropdb(&pg_config, &dbname, false, self.runas.clone())?;
            }
            // won't re-create it if it already exists
            let created_db = createdb(&pg_config, &dbname, false, true, self.runas.clone())?;
            if !created_db {
                println!("{} existing database {dbname}", "    Re-using".bold().cyan());
            }

            // Handle --add: bootstrap a single new test and exit
            if let Some(ref add_name) = self.add {
                return self.execute_add(
                    &pg_config,
                    &manifest_path,
                    &pgregress_path,
                    &dbname,
                    add_name,
                    created_db,
                );
            }

            // figure out what test and output files we have
            let mut test_files = self.list_sql_tests(&manifest_path, created_db)?;
            let output_files = self.list_expected_outputs(&manifest_path, created_db)?;

            // filter tests
            if let Some(test_filter) = self.test_filter.as_ref() {
                test_files.retain(|entry| {
                    let name = make_test_name(entry);
                    // keep setup.sql when the database was just created — it needs to run
                    // even when filtering to a specific test
                    (created_db && name == "setup") || name.contains(test_filter)
                });
                if test_files.is_empty() {
                    println!(
                        "{} no tests matching filter `{test_filter}`",
                        "       ERROR".bold().red()
                    );
                    std::process::exit(1);
                }

                // When the user explicitly filters, error if any matched test
                // has no expected output (they should use --add first)
                let output_names = output_files.iter().map(make_test_name).collect::<HashSet<_>>();
                for entry in &test_files {
                    let name = make_test_name(entry);
                    if !output_names.contains(&name) {
                        eprintln!(
                            "{} test `{}` has no expected output. Run `cargo pgrx regress --add {}` first.",
                            "       ERROR".bold().red(),
                            name.bold().cyan(),
                            name,
                        );
                        std::process::exit(1);
                    }
                }
            }

            println!();
            println!("--- beginning regression test run ---");
            let success = self.run_all_tests(
                &pg_config,
                &manifest_path,
                &pgregress_path,
                &dbname,
                &test_files.iter().collect::<Vec<_>>(),
                &output_files.iter().collect::<Vec<_>>(),
                created_db, // include_setup
                run,
            )?;

            if !success {
                any_failed = true;
            }
        }

        if any_failed {
            std::process::exit(1);
        }

        Ok(())
    }
}

impl Regress {
    /// Resolve the positional `args` vec into `pg_version` and `test_filter`.
    ///
    /// - `cargo pgrx regress` → both None
    /// - `cargo pgrx regress my_test` → test_filter = Some("my_test")
    /// - `cargo pgrx regress pg16` → pg_version = Some("pg16")
    /// - `cargo pgrx regress pg16 my_test` → pg_version = Some("pg16"), test_filter = Some("my_test")
    fn resolve_args(&mut self) {
        match Self::parse_args(&self.args) {
            Ok((pg_version, test_filter)) => {
                self.pg_version = pg_version;
                self.test_filter = test_filter;
            }
            Err(message) => {
                eprintln!("{} {message}", "       ERROR".bold().red());
                std::process::exit(1);
            }
        }
    }

    fn parse_args(args: &[String]) -> Result<(Option<String>, Option<String>), String> {
        fn is_supported_pg_version_label(label: &str) -> bool {
            label
                .strip_prefix("pg")
                .and_then(|major| major.parse::<u16>().ok())
                .is_some_and(is_supported_major_version)
        }

        match args {
            [] => Ok((None, None)),
            [only] if is_supported_pg_version_label(only) => Ok((Some(only.clone()), None)),
            [only] => Ok((None, Some(only.clone()))),
            [first, second] if is_supported_pg_version_label(first) => {
                Ok((Some(first.clone()), Some(second.clone())))
            }
            [first, _second] => Err(format!(
                "first positional argument must be a PostgreSQL version (e.g., pg16), got `{first}`"
            )),
            _ => {
                Err("too many positional arguments. Usage: cargo pgrx regress [pgXX] [testname]"
                    .into())
            }
        }
    }

    /// Handle the --add flag: bootstrap a single new test.
    fn execute_add(
        &self,
        pg_config: &PgConfig,
        manifest_path: &Path,
        pgregress_path: &Path,
        dbname: &str,
        add_name: &str,
        created_db: bool,
    ) -> eyre::Result<()> {
        let test_files = self.list_sql_tests(manifest_path, created_db)?;
        let expected_outputs = self.list_expected_outputs(manifest_path, created_db)?;

        // Find the test file matching --add
        let test_file = test_files.iter().find(|entry| make_test_name(entry) == add_name);
        let Some(test_file) = test_file else {
            eprintln!(
                "{} no test file `{}.sql` found in {}",
                "       ERROR".bold().red(),
                add_name.bold().cyan(),
                manifest_path_to_sql_tests_path(manifest_path).display(),
            );
            std::process::exit(1);
        };

        // Check if the test already has expected output
        if expected_outputs.iter().any(|e| make_test_name(e) == add_name) {
            eprintln!(
                "{} test `{}` already has expected output. Use `cargo pgrx regress` to run it, \
                or `cargo pgrx regress --auto` to update its expected output.",
                "     WARNING".bold().yellow(),
                add_name.bold().cyan(),
            );
            std::process::exit(1);
        }

        // Run setup.sql first if it exists (--add always starts with a fresh DB)
        let setup_files = self.list_sql_tests(manifest_path, true)?;
        let setup_entry = setup_files.iter().find(|e| make_test_name(e) == "setup");
        if let Some(setup_entry) = setup_entry {
            // Check if setup already has expected output; if not, bootstrap it too
            let setup_has_output = expected_outputs.iter().any(|e| make_test_name(e) == "setup");
            if !setup_has_output {
                println!("{} setup.sql (no expected output yet)", "Bootstrapping".bold().green(),);
                self.bootstrap_new_test(
                    pg_config,
                    manifest_path,
                    pgregress_path,
                    dbname,
                    setup_entry,
                )?;
            } else {
                // Run setup.sql normally to establish schema/data
                let verbosity = &self.psql_verbosity.clone().unwrap_or("terse".into());
                run_tests(pg_config, pgregress_path, dbname, &[setup_entry], verbosity, 0)?;
            }
        }

        // Bootstrap the requested test
        self.bootstrap_new_test(pg_config, manifest_path, pgregress_path, dbname, test_file)?;

        println!(
            "\n{} test `{}` bootstrapped. Run `cargo pgrx regress` to verify.",
            "        Done".bold().green(),
            add_name.bold().cyan(),
        );

        Ok(())
    }

    /// Handle --dry-run: print what would happen and exit.
    fn execute_dry_run(&self, manifest_path: &Path) -> eyre::Result<()> {
        let extname = get_property(manifest_path, "extname")?
            .expect("extension name property `extname` should always be known");
        let dbname = self.dbname.clone().unwrap_or_else(|| format!("{extname}_regress"));
        let profile = if self.release { "release" } else { "dev" };

        println!("{}", "--- dry run ---".bold().cyan());
        println!(
            "Would build and install extension '{}' ({} profile)",
            extname.bold().white(),
            profile,
        );

        if let Some(ref pg_version) = self.pg_version {
            println!("Target Postgres version: {}", pg_version.bold().white());
        } else {
            println!("Target Postgres version: (from Cargo.toml default features)");
        }

        if self.resetdb || self.add.is_some() {
            println!("Would {} database '{}'", "drop and recreate".bold().yellow(), dbname.cyan(),);
        } else {
            println!("Would reuse existing database '{}'", dbname.cyan());
        }

        if let Some(ref add_name) = self.add {
            let sql_path =
                manifest_path_to_sql_tests_path(manifest_path).join(format!("{add_name}.sql"));
            if sql_path.exists() {
                println!("Would bootstrap new test: {}", add_name.bold().green());
            } else {
                println!("{} no test file `{}.sql` found", "       ERROR".bold().red(), add_name,);
            }
            return Ok(());
        }

        // List test files (without setup, then report)
        let test_files = self.list_sql_tests(manifest_path, false)?;
        let output_files = self.list_expected_outputs(manifest_path, false)?;
        let output_names = output_files.iter().map(make_test_name).collect::<HashSet<_>>();

        let mut ready = Vec::new();
        let mut skipped = Vec::new();
        for entry in &test_files {
            let name = make_test_name(entry);
            if let Some(ref filter) = self.test_filter
                && !name.contains(filter)
            {
                continue;
            }
            if output_names.contains(&name) {
                ready.push(name);
            } else {
                skipped.push(name);
            }
        }

        if !ready.is_empty() {
            println!("Would run {} tests: {}", ready.len(), ready.join(", "),);
        }
        if !skipped.is_empty() {
            println!(
                "Would skip {} tests without expected output: {}",
                skipped.len(),
                skipped.join(", "),
            );
        }

        if self.auto {
            println!("Would {} failed test output to expected/", "promote".bold().yellow());
        }

        Ok(())
    }
}

fn run_tests(
    pg_config: &PgConfig,
    pg_regress_bin: &Path,
    dbname: &str,
    test_files: &[&DirEntry],
    verbosity: &str,
    skipped_cnt: usize,
) -> eyre::Result<bool> {
    if test_files.is_empty() {
        return Ok(true);
    }
    let input_dir = test_files[0].path();
    let input_dir = input_dir
        .parent()
        .expect("test file should not be at the root of the filesystem")
        .parent()
        .expect("test file should be in a directory named `sql/`")
        .to_path_buf();
    pg_regress(pg_config, pg_regress_bin, dbname, &input_dir, test_files, verbosity, skipped_cnt)
        .map(|status| status.success())
}

fn create_regress_output(
    pg_config: &PgConfig,
    manifest_path: &Path,
    pg_regress_bin: &Path,
    dbname: &str,
    test_file: &DirEntry,
    verbosity: &str,
) -> eyre::Result<Option<PathBuf>> {
    let test_name = make_test_name(test_file);
    let input_dir = test_file.path();
    let input_dir = input_dir
        .parent()
        .expect("test file should not be at the root of the filesystem")
        .parent()
        .expect("test file should be in a directory named `sql/`")
        .to_path_buf();
    let status =
        pg_regress(pg_config, pg_regress_bin, dbname, &input_dir, &[test_file], verbosity, 0)?;

    if !status.success() {
        // pg_regress returned with an error code, but that is most likely because the test's output file
        // doesn't exist, since we are creating the test output.  So if that's the case, if we have
        // a `.out` file for it in the results/ directory, then we're successful
        let out_file =
            manifest_path_to_results_output_path(manifest_path).join(format!("{test_name}.out"));
        if out_file.exists() {
            return Ok(Some(out_file));
        } else {
            std::process::exit(status.code().unwrap_or(1));
        }
    }

    Ok(None)
}

fn pg_regress(
    pg_config: &PgConfig,
    bin: &Path,
    dbname: &str,
    input_dir: &Path,
    tests: &[&DirEntry],
    verbosity: &str,
    skipped_cnt: usize,
) -> eyre::Result<ExitStatus> {
    if tests.is_empty() {
        eyre::bail!("no tests to run");
    }
    let test_dir = tests[0].path().parent().unwrap().parent().unwrap().to_path_buf();
    let tests = tests.iter().map(|entry| make_test_name(entry));

    let mut command = Command::new(bin);
    command
        .current_dir(test_dir)
        .env_remove("PGDATABASE")
        .env_remove("PGHOST")
        .env_remove("PGPORT")
        .env_remove("PGUSER")
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .arg("--host")
        .arg(pg_config.host())
        .arg("--port")
        .arg(pg_config.port()?.to_string())
        .arg("--use-existing")
        .arg(format!("--dbname={dbname}"))
        .arg(format!("--inputdir={}", input_dir.display()))
        .arg(format!("--outputdir={}", input_dir.display()))
        .args(tests);

    #[cfg(not(target_os = "windows"))]
    let launcher_script = {
        fn make_launcher_script(verbosity: &str) -> eyre::Result<PathBuf> {
            use std::os::unix::fs::PermissionsExt;

            let launcher_script =
                format!("#! /bin/bash\n$* -v VERBOSITY={}", verbosity,).into_bytes();

            let path = temp_dir().join(format!("pgrx-pg_regress-runner-{}.sh", std::process::id()));
            let mut tmpfile = File::create(&path)?;
            tmpfile.write_all(&launcher_script)?;
            let mut perms = path.metadata()?.permissions();
            perms.set_mode(0o700);
            tmpfile.set_permissions(perms)?;
            Ok(path)
        }
        let launcher_script = make_launcher_script(verbosity)?;
        command.arg(format!("--launcher={}", launcher_script.display()));
        launcher_script
    };

    tracing::trace!("running {command:?}");

    let mut child = command.spawn()?;
    let (Some(stdout), Some(stderr)) = (child.stdout.take(), child.stderr.take()) else {
        panic!("unable to take stdout or stderr from pg_regress process");
    };

    let output_monitor = std::thread::spawn(move || {
        let mut passed_cnt = 0;
        let mut failed_cnt = 0;
        let stdout = BufReader::new(stdout);
        let stderr = BufReader::new(stderr);
        for line in stdout.lines().chain(stderr.lines()) {
            let line = line.unwrap();
            let Some((line, result)) = decorate_output(line) else {
                continue;
            };

            match result {
                Some(TestResult::Passed) => passed_cnt += 1,
                Some(TestResult::Failed) => failed_cnt += 1,
                None => (),
            }

            println!("{line}");
        }
        (passed_cnt, failed_cnt)
    });
    let status = child.wait()?;
    let (passed_cnt, failed_cnt) =
        output_monitor.join().map_err(|_| eyre::eyre!("failed to join output monitor thread"))?;
    if skipped_cnt > 0 {
        println!("passed={passed_cnt} failed={failed_cnt} skipped={skipped_cnt}");
    } else {
        println!("passed={passed_cnt} failed={failed_cnt}");
    }

    #[cfg(not(target_os = "windows"))]
    {
        std::fs::remove_file(launcher_script)?;
    }

    Ok(status)
}

/// Show the regression diffs path on failure. With `-v`, also print the full
/// diff content to stderr.  When `repeat > 1`, rename the file to
/// `regression.<run>.diffs` so each attempt's diffs are preserved.
fn print_regression_diffs(manifest_path: &Path, verbose: u8, run: u32, repeat: u32) {
    // pg_regress writes regression.diffs to --outputdir, which is the pg_regress/ directory
    let diffs_path = manifest_path_to_pg_regress_dir(manifest_path).join("regression.diffs");
    if !diffs_path.exists() {
        return;
    }

    if verbose >= 1
        && let Ok(content) = std::fs::read_to_string(&diffs_path)
    {
        eprintln!();
        eprintln!("{content}");
    }

    // When repeating, rename to regression.<run>.diffs so each run's output is preserved
    let final_path = if repeat > 1 {
        let renamed =
            manifest_path_to_pg_regress_dir(manifest_path).join(format!("regression.{run}.diffs"));
        // remove any stale file with the same name first
        let _ = std::fs::remove_file(&renamed);
        if let Err(e) = std::fs::rename(&diffs_path, &renamed) {
            eprintln!(
                "{} failed to rename regression.diffs to {}: {e}",
                "     WARNING".bold().yellow(),
                renamed.display()
            );
            diffs_path
        } else {
            renamed
        }
    } else {
        diffs_path
    };

    eprintln!("\n{} {}", "  Diffs at".bold().red(), final_path.display().bold().cyan());
}

enum TestResult {
    Passed,
    Failed,
}

fn decorate_output(mut line: String) -> Option<(String, Option<TestResult>)> {
    let mut decorated = String::with_capacity(line.len());
    let mut test_result: Option<TestResult> = None;
    let mut is_old_line = false;
    let mut is_new_line = false;

    if line.starts_with("ok") {
        // for pg_regress from pg16 forward, rewrite the "ok" into a colored PASS"
        is_new_line = true;
    } else if line.starts_with("not ok") {
        // for pg_regress from pg16 forward, rewrite the "no ok" into a colored FAIL"
        line = line.replace("not ok", "not_ok"); // to make parsing easier down below
        is_new_line = true;
    } else if line.contains("... ok") || line.contains("... FAILED") {
        is_old_line = true;
    }

    let parsed_test_line = if is_new_line {
        fn split_line(line: &str) -> Option<(&str, bool, &str, &str)> {
            let mut parts = line.split_whitespace();

            let passed = parts.next()? == "ok";
            parts.next()?; // throw away the test number
            parts.next()?; // throw away the dash (-)
            let test_name = parts.next()?;
            let execution_time = parts.next()?;
            let execution_units = parts.next()?;
            Some((test_name, passed, execution_time, execution_units))
        }
        split_line(&line)
    } else if is_old_line {
        fn split_line(line: &str) -> Option<(&str, bool, &str, &str)> {
            let mut parts = line.split_whitespace();

            parts.next()?; // throw away "test"
            let test_name = parts.next()?;
            parts.next()?; // throw away "..."
            let passed = parts.next()? == "ok";
            let execution_time = parts.next()?;
            let execution_units = parts.next()?;
            Some((test_name, passed, execution_time, execution_units))
        }
        split_line(&line)
    } else {
        // not a line we care about
        return None;
    };

    if let Some((test_name, passed, execution_time, execution_units)) = parsed_test_line {
        if passed {
            test_result = Some(TestResult::Passed);
        } else {
            test_result = Some(TestResult::Failed);
        }

        decorated.push_str(&format!(
            "{} {test_name} {execution_time}{execution_units}",
            if passed {
                "PASS".bold().bright_green().to_string()
            } else {
                "FAIL".bold().bright_red().to_string()
            }
        ))
    }

    Some((decorated, test_result))
}

fn make_test_name(entry: &DirEntry) -> String {
    let filename = entry.file_name();
    let filename = filename.to_str().unwrap_or_else(|| panic!("bogus file name: {entry:?}"));
    let filename =
        filename.split('.').next().unwrap_or_else(|| panic!("invalid filename: `{filename}`"));
    filename.to_string()
}

fn manifest_path_to_sql_tests_path(manifest_path: &Path) -> PathBuf {
    let mut path = PathBuf::from(manifest_path);
    path.pop(); // pop `Cargo.toml`
    path.push("tests");
    path.push("pg_regress");
    path.push("sql");
    path
}

fn manifest_path_to_expected_tests_output_path(manifest_path: &Path) -> PathBuf {
    let mut path = PathBuf::from(manifest_path);
    path.pop(); // pop `Cargo.toml`
    path.push("tests");
    path.push("pg_regress");
    path.push("expected");
    path
}

fn manifest_path_to_results_output_path(manifest_path: &Path) -> PathBuf {
    let mut path = PathBuf::from(manifest_path);
    path.pop(); // pop `Cargo.toml`
    path.push("tests");
    path.push("pg_regress");
    path.push("results");
    path
}

fn manifest_path_to_pg_regress_dir(manifest_path: &Path) -> PathBuf {
    let mut path = PathBuf::from(manifest_path);
    path.pop(); // pop `Cargo.toml`
    path.push("tests");
    path.push("pg_regress");
    path
}

fn add_to_git(path: &Path) -> eyre::Result<()> {
    if let Ok(git) = which::which("git")
        && is_git_repo(&git)
        && !Command::new(git).arg("add").arg(path).status()?.success()
    {
        panic!("unable to add {} to git", path.display());
    }
    Ok(())
}

fn is_git_repo(git: &Path) -> bool {
    Command::new(git)
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .arg("rev-parse")
        .arg("--is-inside-work-tree")
        .status()
        .map(|status| status.success())
        .unwrap_or_default()
}

#[cfg(test)]
mod tests {
    use super::Regress;

    fn strings(args: &[&str]) -> Vec<String> {
        args.iter().map(|arg| (*arg).to_string()).collect()
    }

    #[test]
    fn parse_args_treats_single_non_version_as_test_filter() {
        let (pg_version, test_filter) = Regress::parse_args(&strings(&["cursor_coverage"]))
            .expect("single test name should parse");

        assert_eq!(pg_version, None);
        assert_eq!(test_filter.as_deref(), Some("cursor_coverage"));
    }

    #[test]
    fn parse_args_accepts_pg_version_then_test_filter() {
        let (pg_version, test_filter) = Regress::parse_args(&strings(&["pg16", "cursor_coverage"]))
            .expect("pg version plus test filter should parse");

        assert_eq!(pg_version.as_deref(), Some("pg16"));
        assert_eq!(test_filter.as_deref(), Some("cursor_coverage"));
    }

    #[test]
    fn parse_args_rejects_test_filter_then_pg_version() {
        let err = Regress::parse_args(&strings(&["cursor_coverage", "pg16"]))
            .expect_err("reversed positional order should still fail");

        assert_eq!(
            err,
            "first positional argument must be a PostgreSQL version (e.g., pg16), got `cursor_coverage`"
        );
    }

    #[test]
    fn parse_args_does_not_treat_unsupported_pg_label_as_version() {
        let (pg_version, test_filter) = Regress::parse_args(&strings(&["pg99"]))
            .expect("unsupported pg label should fall back to test filter");

        assert_eq!(pg_version, None);
        assert_eq!(test_filter.as_deref(), Some("pg99"));
    }
}
